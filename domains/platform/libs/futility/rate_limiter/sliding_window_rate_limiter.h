#ifndef DOMAINS_API_PLATFORM_LIBS_FUTILITY_RATE_LIMITER_SLIDING_WINDOW_RATE_LIMITER_H
#define DOMAINS_API_PLATFORM_LIBS_FUTILITY_RATE_LIMITER_SLIDING_WINDOW_RATE_LIMITER_H

/// @file sliding_window_rate_limiter.h
/// @brief Thread-safe per-key sliding window rate limiter.
///
/// This rate limiter uses a sliding window algorithm that provides smoother rate limiting
/// compared to fixed window approaches. It interpolates between the previous and current
/// window counts based on elapsed time, preventing the "burst at window boundary" problem.
///
/// Example usage:
/// @code
///   SlidingWindowRateLimiterConfig config{
///     .max_requests_per_key = 100,
///     .window_size = std::chrono::seconds(60),
///     .ttl = std::chrono::minutes(5),
///     .cleanup_interval = std::chrono::seconds(30),
///     .max_keys = 10000
///   };
///   SlidingWindowRateLimiter<std::string> limiter(config);
///
///   std::string client_ip = "192.168.1.1";
///   if (limiter.allow(client_ip)) {
///     // Process request
///   } else {
///     // Reject with 429 Too Many Requests
///   }
/// @endcode

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace futility::rate_limiter {

/// @brief Internal state for tracking a single key's rate limit window.
/// @note This is an implementation detail and should not be used directly.
struct WindowState {
  std::mutex mutex;                                    ///< Per-key mutex for thread safety
  long previous_count{0};                              ///< Request count from previous window
  long current_count{0};                               ///< Request count in current window
  std::chrono::steady_clock::time_point window_start;  ///< Start time of current window
  std::chrono::steady_clock::time_point last_access;   ///< Last access time (for TTL eviction)

  explicit WindowState(std::chrono::steady_clock::time_point now)
      : window_start(now), last_access(now) {}
};

/// @brief Configuration for SlidingWindowRateLimiter.
struct SlidingWindowRateLimiterConfig {
  /// Maximum number of requests allowed per key within the window.
  /// Must be positive.
  long max_requests_per_key;

  /// Duration of the sliding window.
  /// Must be positive.
  std::chrono::milliseconds window_size;

  /// Time-to-live for inactive keys. Keys not accessed within this duration
  /// are eligible for eviction during cleanup.
  /// Default: 5 minutes.
  std::chrono::milliseconds ttl = std::chrono::minutes(5);

  /// Interval between cleanup runs that evict expired keys.
  /// Lower values reduce memory usage but increase overhead.
  /// Default: 30 seconds.
  std::chrono::milliseconds cleanup_interval = std::chrono::seconds(30);

  /// Optional maximum number of unique keys to track.
  /// When set, new keys are rejected if this limit is reached.
  /// Useful for preventing memory exhaustion from key cardinality attacks.
  /// Default: no limit (nullopt).
  std::optional<size_t> max_keys = std::nullopt;
};

/// @brief Thread-safe per-key sliding window rate limiter.
///
/// This class implements a sliding window rate limiting algorithm that tracks
/// request counts per key (e.g., IP address, user ID, API key). The sliding window
/// approach interpolates between two fixed windows to provide smoother rate limiting.
///
/// Thread Safety:
/// - All public methods are thread-safe.
/// - Uses a shared mutex for the key map and per-key mutexes for individual states.
/// - Suitable for high-concurrency scenarios.
///
/// Memory Management:
/// - Automatically evicts keys that haven't been accessed within the TTL.
/// - Optionally limits the total number of tracked keys via max_keys.
/// - Cleanup runs lazily during allow() calls, not in a background thread.
///
/// @tparam Key The type used to identify rate limit buckets (e.g., std::string, int).
///             Must be hashable (usable as unordered_map key).
/// @tparam Clock The clock type for time measurements. Defaults to std::chrono::steady_clock.
///               Can be substituted with a mock clock for testing.
template <typename Key, typename Clock = std::chrono::steady_clock>
class SlidingWindowRateLimiter {
 public:
  /// @brief Constructs a rate limiter with the given configuration.
  /// @param config The rate limiter configuration.
  /// @throws std::invalid_argument if any configuration value is invalid:
  ///         - max_requests_per_key <= 0
  ///         - window_size <= 0
  ///         - ttl <= 0
  ///         - cleanup_interval <= 0
  ///         - max_keys == 0 (if specified)
  explicit SlidingWindowRateLimiter(const SlidingWindowRateLimiterConfig& config)
      : max_requests_per_key_(config.max_requests_per_key),
        window_size_(config.window_size),
        cleanup_interval_(config.cleanup_interval),
        ttl_(config.ttl),
        max_keys_(config.max_keys),
        last_cleanup_(Clock::now()) {
    if (config.max_requests_per_key <= 0) {
      throw std::invalid_argument("max_requests_per_key must be positive");
    }
    if (config.window_size <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("window_size must be positive");
    }
    if (config.ttl <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("ttl must be positive");
    }
    if (config.cleanup_interval <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("cleanup_interval must be positive");
    }
    if (config.max_keys && *config.max_keys == 0) {
      throw std::invalid_argument("max_keys must be positive if specified");
    }
  }

  /// @brief Checks if a request should be allowed and consumes quota if so.
  ///
  /// This method atomically checks whether the request can be allowed within
  /// the rate limit and, if so, increments the request count. The check uses
  /// a weighted average of the previous and current window counts.
  ///
  /// @param key The identifier for the rate limit bucket.
  /// @param cost The cost of this request (default: 1). Useful for requests
  ///             that should consume more quota (e.g., batch operations).
  /// @return true if the request is allowed, false if rate limited.
  /// @note Returns false if max_keys is set and the limit is reached for new keys.
  /// @note Calling allow() on a key updates its last_access time, preventing eviction.
  bool allow(const Key& key, long cost = 1) {
    auto state_ptr = get_or_create_state(key);
    if (!state_ptr) {
      return false;  // Max keys limit exceeded
    }

    auto& state = *state_ptr;
    std::lock_guard<std::mutex> lock(state.mutex);

    maybe_slide_window(state);

    double weighted = get_weighted_count(state);
    if (weighted + cost > max_requests_per_key_) {
      return false;
    }

    state.current_count += cost;
    return true;
  }

 private:
  WindowState* get_or_create_state(const Key& key) {
    maybe_cleanup();

    {
      std::shared_lock lock{map_mutex_};
      if (limiters_.contains(key)) {
        return limiters_.at(key).get();
      }
    }

    std::unique_lock lock{map_mutex_};
    if (limiters_.contains(key)) {
      return limiters_.at(key).get();
    }

    // Check max keys limit before creating new state
    if (max_keys_ && limiters_.size() >= *max_keys_) {
      return nullptr;
    }

    auto [iterator, _] = limiters_.emplace(key, std::make_unique<WindowState>(Clock::now()));
    return iterator->second.get();
  }

  void maybe_cleanup() {
    auto now = Clock::now();
    if (now - last_cleanup_ < cleanup_interval_) {
      return;
    }

    std::unique_lock lock{map_mutex_};
    if (now - last_cleanup_ < cleanup_interval_) {
      return;
    }

    auto cutoff = now - ttl_;
    std::erase_if(limiters_, [cutoff](const auto& pair) {
      std::lock_guard<std::mutex> lock(pair.second->mutex);
      return pair.second->last_access < cutoff;
    });
    last_cleanup_ = now;
  }

  void maybe_slide_window(WindowState& state) {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.window_start);

    if (elapsed >= window_size_) {
      if (elapsed >= 2 * window_size_) {
        // Long idle period - reset both windows
        state.previous_count = 0;
        state.current_count = 0;
        state.window_start = now;
      } else {
        // Normal slide - move to next window
        state.previous_count = state.current_count;
        state.current_count = 0;
        state.window_start += window_size_;
      }
    }
    state.last_access = now;
  }

  double get_weighted_count(const WindowState& state) const {
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.window_start);

    double elapsed_ratio =
        static_cast<double>(elapsed.count()) / static_cast<double>(window_size_.count());
    elapsed_ratio = std::min(1.0, elapsed_ratio);

    return state.previous_count * (1.0 - elapsed_ratio) + state.current_count;
  }

  mutable std::shared_mutex map_mutex_;
  std::unordered_map<Key, std::unique_ptr<WindowState>> limiters_;
  const long max_requests_per_key_;
  const std::chrono::milliseconds window_size_;
  const std::chrono::milliseconds cleanup_interval_;
  const std::chrono::milliseconds ttl_;
  const std::optional<size_t> max_keys_;
  typename Clock::time_point last_cleanup_;
};

}  // namespace futility::rate_limiter

#endif
