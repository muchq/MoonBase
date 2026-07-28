#include "domains/games/apis/golf_hub/rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>

namespace golf_hub {
namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

// All time is fabricated: the bucket's contract is about elapsed time it
// is told about, never about the wall clock.
const steady_clock::time_point kEpoch{};

TEST(TokenBucketTest, GrantsTheBurstThenRefuses) {
  TokenBucket bucket(/*capacity=*/3, /*refill_per_sec=*/1);
  EXPECT_TRUE(bucket.Admit(kEpoch));
  EXPECT_TRUE(bucket.Admit(kEpoch));
  EXPECT_TRUE(bucket.Admit(kEpoch));
  EXPECT_FALSE(bucket.Admit(kEpoch));
  // A refusal costs nothing: still refused, not doubly indebted.
  EXPECT_FALSE(bucket.Admit(kEpoch));
}

TEST(TokenBucketTest, RefillsAtTheSustainedRate) {
  TokenBucket bucket(/*capacity=*/2, /*refill_per_sec=*/2);
  EXPECT_TRUE(bucket.Admit(kEpoch));
  EXPECT_TRUE(bucket.Admit(kEpoch));
  EXPECT_FALSE(bucket.Admit(kEpoch));

  // 500ms at 2/s is one whole token.
  EXPECT_TRUE(bucket.Admit(kEpoch + milliseconds(500)));
  EXPECT_FALSE(bucket.Admit(kEpoch + milliseconds(500)));
}

TEST(TokenBucketTest, FractionalRefillsAccumulate) {
  TokenBucket bucket(/*capacity=*/1, /*refill_per_sec=*/1);
  EXPECT_TRUE(bucket.Admit(kEpoch));
  // Four quarter-second refusals each bank 0.25 tokens...
  EXPECT_FALSE(bucket.Admit(kEpoch + milliseconds(250)));
  EXPECT_FALSE(bucket.Admit(kEpoch + milliseconds(500)));
  EXPECT_FALSE(bucket.Admit(kEpoch + milliseconds(750)));
  // ...and the fourth quarter completes the token.
  EXPECT_TRUE(bucket.Admit(kEpoch + milliseconds(1000)));
}

TEST(TokenBucketTest, RefillCapsAtCapacity) {
  TokenBucket bucket(/*capacity=*/2, /*refill_per_sec=*/1000);
  EXPECT_TRUE(bucket.Admit(kEpoch));
  // An hour idle banks no more than the burst.
  const auto later = kEpoch + std::chrono::hours(1);
  EXPECT_TRUE(bucket.Admit(later));
  EXPECT_TRUE(bucket.Admit(later));
  EXPECT_FALSE(bucket.Admit(later));
}

TEST(TokenBucketTest, TimeMovingBackwardsRefillsNothing) {
  TokenBucket bucket(/*capacity=*/1, /*refill_per_sec=*/1000);
  EXPECT_TRUE(bucket.Admit(kEpoch + milliseconds(1000)));
  // An earlier reading must not mint tokens (steady_clock shouldn't do
  // this, but the bucket must not amplify a platform bug into free
  // budget).
  EXPECT_FALSE(bucket.Admit(kEpoch));
  EXPECT_FALSE(bucket.Admit(kEpoch + milliseconds(1000)));
}

}  // namespace
}  // namespace golf_hub
