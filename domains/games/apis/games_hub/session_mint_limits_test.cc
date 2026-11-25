#include <chrono>
#include <string>

#include "domains/games/apis/games_hub/rate_limiter.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "gtest/gtest.h"

namespace games_hub {
namespace {

class FakeClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::steady_clock::time_point;
  static time_point now() { return now_; }
  static void Advance(duration d) { now_ += d; }

 private:
  static inline time_point now_{};
};

using MintLimiter = futility::rate_limiter::SlidingWindowRateLimiter<std::string, FakeClock>;

// A launch: two hundred new players a second for ten minutes, each
// minting once. None of them is refused for being new.
TEST(SessionMintLimitsTest, ALaunchesNewClientsAreEachAdmitted) {
  MintLimiter limiter(SessionMintLimits());
  int refused = 0;
  int client = 0;
  for (int second = 0; second < 600; ++second) {
    for (int i = 0; i < 200; ++i) {
      if (!limiter.allow("client-" + std::to_string(client++))) ++refused;
    }
    FakeClock::Advance(std::chrono::seconds(1));
  }
  EXPECT_EQ(refused, 0);
}

TEST(SessionMintLimitsTest, OneClientPastTwentyMintsAMinuteIsRefused) {
  MintLimiter limiter(SessionMintLimits());
  for (int i = 0; i < 20; ++i) EXPECT_TRUE(limiter.allow("busy")) << i;
  EXPECT_FALSE(limiter.allow("busy"));
}

// Forgetting a client never hands it budget back early: by the time a key
// is idle past its TTL, the sliding window has already emptied it.
TEST(SessionMintLimitsTest, AClientIsForgottenOnlyOnceItsWindowIsEmpty) {
  const auto limits = SessionMintLimits();
  EXPECT_GE(limits.ttl, 2 * limits.window_size);
}

}  // namespace
}  // namespace games_hub
