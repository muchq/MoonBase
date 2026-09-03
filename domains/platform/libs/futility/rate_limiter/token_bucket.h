#ifndef DOMAINS_API_PLATFORM_LIBS_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_H
#define DOMAINS_API_PLATFORM_LIBS_FUTILITY_RATE_LIMITER_TOKEN_BUCKET_H

/// @file token_bucket.h
/// @brief An unsynchronized single-owner token bucket with injected time.
///
/// The third shape in this directory, distinct on purpose:
///  - SlidingWindowRateLimiter: per-key, thread-safe, self-clocked — the
///    keyed guard for HTTP surfaces (aura's allow_request).
///  - TokenBucketRateLimiter: global, thread-safe, self-clocked.
///  - TokenBucket (this): one bucket, no lock, caller-supplied clock
///    readings — for budgets owned by exactly one execution context,
///    like a per-session stream budget living in its coroutine frame
///    (games_hub, MoonBase#1240), where frames are handled sequentially
///    and a mutex or clock call per admit would be pure overhead. The
///    injected time is also what makes refill behavior testable with
///    fabricated clocks instead of sleeps.

#include <algorithm>
#include <chrono>
#include <optional>

namespace futility::rate_limiter {

class TokenBucket {
 public:
  /// A bucket that starts full. `capacity` is the burst; `refill_per_sec`
  /// is the sustained rate. Tokens are fractional so slow refills grant
  /// eventually rather than never.
  TokenBucket(double capacity, double refill_per_sec)
      : capacity_(capacity), refill_per_sec_(refill_per_sec), tokens_(capacity) {}

  /// Spends one token if the bucket (after refilling for elapsed time)
  /// holds one; a refusal costs nothing. `now` must come from a
  /// monotonic clock; a reading at or before the last one refills
  /// nothing and does not move the watermark back — elapsed time is
  /// only ever counted once.
  bool Admit(std::chrono::steady_clock::time_point now) {
    if (!last_.has_value()) {
      last_ = now;
    } else if (now > *last_) {
      const std::chrono::duration<double> elapsed = now - *last_;
      tokens_ = std::min(capacity_, tokens_ + elapsed.count() * refill_per_sec_);
      last_ = now;
    }
    if (tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    return true;
  }

 private:
  const double capacity_;
  const double refill_per_sec_;
  double tokens_;
  std::optional<std::chrono::steady_clock::time_point> last_;
};

}  // namespace futility::rate_limiter

#endif
