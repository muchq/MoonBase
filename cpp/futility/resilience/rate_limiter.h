#ifndef CPP_FUTILITY_RESILIENCE_RATE_LIMITER_H
#define CPP_FUTILITY_RESILIENCE_RATE_LIMITER_H

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace futility::resilience {
struct WindowState {
  std::mutex mutex;
  long previous_count{0};
  long current_count{0};
  std::chrono::steady_clock::time_point window_start;
  std::chrono::steady_clock::time_point last_access;

  WindowState(std::chrono::steady_clock::time_point now) : window_start(now), last_access(now) {}
};

template <typename Key, typename Clock = std::chrono::steady_clock>
class SlidingWindowRateLimiter {
 public:
  explicit SlidingWindowRateLimiter(
      long max_requests_per_key, std::chrono::milliseconds window_size,
      std::chrono::milliseconds ttl = std::chrono::minutes(5),
      std::chrono::milliseconds cleanup_interval = std::chrono::seconds(30),
      std::optional<size_t> max_keys = std::nullopt)
      : max_requests_per_key_(max_requests_per_key),
        window_size_(window_size),
        cleanup_interval_(cleanup_interval),
        ttl_(ttl),
        max_keys_(max_keys),
        last_cleanup_(Clock::now()) {
    if (max_requests_per_key <= 0) {
      throw std::invalid_argument("max_requests_per_key must be positive");
    }
    if (window_size <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("window_size must be positive");
    }
    if (ttl <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("ttl must be positive");
    }
    if (cleanup_interval <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("cleanup_interval must be positive");
    }
    if (max_keys && *max_keys == 0) {
      throw std::invalid_argument("max_keys must be positive if specified");
    }
  }

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

}  // namespace futility::resilience

#endif
