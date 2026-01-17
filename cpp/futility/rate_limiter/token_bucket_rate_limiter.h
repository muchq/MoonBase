#ifndef CPP_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_RATE_LIMITER_H
#define CPP_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_RATE_LIMITER_H

/// @file token_bucket_rate_limiter.h
/// @brief Thread-safe token bucket rate limiter for global rate limiting.
///
/// The token bucket algorithm allows bursts of traffic up to a maximum capacity
/// while enforcing a sustained rate limit. Tokens are added at a fixed rate and
/// consumed by requests. Unlike the sliding window limiter, this is designed for
/// global (not per-key) rate limiting.
///
/// Example usage:
/// @code
///   TokenBucketConfig config{
///     .refill_rate_seconds = 100,  // Add 100 tokens per second
///     .max_tokens = 500            // Allow bursts up to 500 requests
///   };
///   TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);
///
///   if (limiter.allow(1)) {
///     // Process request
///   } else {
///     // Reject with 429 Too Many Requests
///   }
/// @endcode

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>

namespace futility::rate_limiter {

/// @brief Configuration for TokenBucketRateLimiter.
struct TokenBucketConfig {
  /// Rate at which tokens are added to the bucket, in tokens per second.
  /// Higher values allow higher sustained throughput.
  long refill_rate_seconds{0};

  /// Maximum number of tokens the bucket can hold.
  /// This determines the maximum burst size allowed.
  long max_tokens{0};
};

/// @brief Thread-safe token bucket rate limiter.
///
/// This class implements the token bucket algorithm for rate limiting.
/// Tokens accumulate over time at a fixed rate up to a maximum capacity.
/// Each request consumes tokens; requests are rejected when insufficient
/// tokens are available.
///
/// Key characteristics:
/// - Allows bursts up to max_tokens
/// - Enforces sustained rate of refill_rate_seconds tokens/second
/// - Tokens accumulate when idle, enabling burst handling
/// - Global limiter (not per-key; use SlidingWindowRateLimiter for per-key)
///
/// Thread Safety:
/// - All public methods are thread-safe via mutex protection.
///
/// @tparam Clock The clock type for time measurements. Use std::chrono::steady_clock
///               for production or a mock clock for testing.
template <typename Clock>
class TokenBucketRateLimiter {
 public:
  /// @brief Constructs a token bucket rate limiter.
  /// @param config The rate limiter configuration.
  explicit TokenBucketRateLimiter(const TokenBucketConfig& config)
      : config_(std::make_unique<TokenBucketConfig>(config)),
        last_refill_(clock_.now()),
        current_tokens_(config.max_tokens) {}

  /// @brief Checks if a request should be allowed and consumes tokens if so.
  ///
  /// Atomically checks if sufficient tokens are available and consumes them.
  /// Tokens are refilled based on elapsed time before checking availability.
  ///
  /// @param cost The number of tokens to consume (typically 1 per request).
  /// @return true if the request is allowed (tokens consumed), false otherwise.
  bool allow(long cost) {
    std::unique_lock<std::mutex> lock(m_);
    refill();

    auto double_cost = static_cast<double>(cost);
    if (current_tokens_ >= double_cost) {
      current_tokens_ -= double_cost;
      return true;
    }
    return false;
  }

 private:
  /// @brief Refills tokens based on elapsed time since last refill.
  void refill() {
    auto now = clock_.now();
    auto to_add = (now - last_refill_).count() * config_->refill_rate_seconds / 1e9;
    if (to_add < 1.0) {
      return;
    }

    current_tokens_ = std::min(current_tokens_ + to_add, static_cast<double>(config_->max_tokens));
    last_refill_ = now;
  }

  std::unique_ptr<TokenBucketConfig> config_;
  std::chrono::steady_clock::time_point last_refill_;
  double current_tokens_{0.0};
  Clock clock_;
  std::mutex m_;
};

}  // namespace futility::rate_limiter

#endif