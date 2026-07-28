#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_RATE_LIMITER_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_RATE_LIMITER_H

#include <algorithm>
#include <chrono>
#include <optional>

namespace golf_hub {

/// A token bucket for the stream's per-session budgets (#1240). Not
/// thread-safe on purpose: each Play coroutine owns its buckets in the
/// coroutine frame, frames are processed sequentially per session, and
/// the state dies with the connection — no shared map, no lock.
///
/// Callers pass the clock reading in, so tests pin refill behavior with
/// fabricated time instead of sleeping.
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

/// The stream's budgets, injectable so tests pin behavior with tiny or
/// huge values instead of racing wall-clock refills. Defaults are far
/// above human play — moves are turn-gated and chat is typed — and far
/// below what it takes to hurt the hub mutex or the database (#1234).
///
/// Every inbound frame draws from the command bucket; chat frames draw
/// from the chat bucket too. Chat is tighter because each message is a
/// durable PostgreSQL transaction plus fleet-wide NOTIFY fan-out, and a
/// rate that is fine for gameplay is still a flood in a conversation.
struct RateLimits {
  double command_burst = 10;
  double command_refill_per_sec = 5;
  double chat_burst = 3;
  double chat_refill_per_sec = 1;
};

}  // namespace golf_hub

#endif
