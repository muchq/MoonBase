#ifndef CPP_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_RATE_LIMITER_H
#define CPP_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_RATE_LIMITER_H

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>

namespace futility::rate_limiter {

struct TokenBucketConfig {
  long refill_rate_seconds{0};
  long max_tokens{0};
};

template <typename Clock>
class TokenBucketRateLimiter {
 public:
  explicit TokenBucketRateLimiter(const TokenBucketConfig& config)
      : config_(std::make_unique<TokenBucketConfig>(config)),
        last_refill_(clock_.now()),
        current_tokens_(config.max_tokens) {}

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