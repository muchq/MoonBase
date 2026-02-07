#include "domains/platform/libs/futility/rate_limiter/token_bucket_rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace futility::rate_limiter {

class MockClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using rep = std::chrono::steady_clock::rep;
  using period = std::chrono::steady_clock::period;
  using time_point = std::chrono::steady_clock::time_point;
  static constexpr bool is_steady = true;

  static time_point now() { return current_time_; }

  static void set_time(time_point t) { current_time_ = t; }

  static void advance_time(duration d) { current_time_ += d; }

 private:
  static time_point current_time_;
};

MockClock::time_point MockClock::current_time_ = std::chrono::steady_clock::now();

class TokenBucketRateLimiterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config.refill_rate_seconds = 10;
    config.max_tokens = 100;
  }

  TokenBucketConfig config;
};

TEST_F(TokenBucketRateLimiterTest, BasicAllowWithinBudget) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  EXPECT_TRUE(limiter.allow(50));
  EXPECT_TRUE(limiter.allow(25));
  EXPECT_TRUE(limiter.allow(10));
}

TEST_F(TokenBucketRateLimiterTest, RejectWhenOverBudget) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  EXPECT_TRUE(limiter.allow(50));
  EXPECT_TRUE(limiter.allow(40));

  EXPECT_FALSE(limiter.allow(20));
}

TEST_F(TokenBucketRateLimiterTest, ExactBudgetUsage) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  EXPECT_TRUE(limiter.allow(100));
  EXPECT_FALSE(limiter.allow(1));
}

TEST_F(TokenBucketRateLimiterTest, ZeroCostAlwaysAllowed) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  limiter.allow(100);

  EXPECT_TRUE(limiter.allow(0));
  EXPECT_TRUE(limiter.allow(0));
}

TEST_F(TokenBucketRateLimiterTest, RefillRestoresTokensWithMockClock) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 100;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(80));
  EXPECT_FALSE(limiter.allow(30));

  MockClock::advance_time(std::chrono::seconds(1));

  EXPECT_TRUE(limiter.allow(20));
}

TEST_F(TokenBucketRateLimiterTest, RefillDoesNotExceedMaxTokens) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 50;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(25));

  MockClock::advance_time(std::chrono::seconds(1));

  EXPECT_TRUE(limiter.allow(25));
  EXPECT_FALSE(limiter.allow(26));
}

TEST_F(TokenBucketRateLimiterTest, SmallRefillAmountIgnored) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 100;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(90));

  MockClock::advance_time(std::chrono::milliseconds(500));

  EXPECT_FALSE(limiter.allow(20));
}

TEST_F(TokenBucketRateLimiterTest, LargeRefillAmount) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 100;
  test_config.max_tokens = 200;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(150));

  MockClock::advance_time(std::chrono::milliseconds(500));

  EXPECT_TRUE(limiter.allow(50));
}

TEST_F(TokenBucketRateLimiterTest, ConcurrentAccess) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 1000;

  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(test_config);

  const int num_threads = 10;
  const int requests_per_thread = 50;
  std::vector<std::future<int>> futures;

  for (int t = 0; t < num_threads; ++t) {
    futures.push_back(std::async(std::launch::async, [&limiter]() {
      int allowed_count = 0;
      for (int i = 0; i < requests_per_thread; ++i) {
        if (limiter.allow(10)) {
          ++allowed_count;
        }
      }
      return allowed_count;
    }));
  }

  int total_allowed = 0;
  for (auto& future : futures) {
    total_allowed += future.get();
  }

  EXPECT_LE(total_allowed, 100);
  EXPECT_GT(total_allowed, 0);
}

TEST_F(TokenBucketRateLimiterTest, LargeCostRequestRejected) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  EXPECT_FALSE(limiter.allow(150));

  EXPECT_TRUE(limiter.allow(50));
}

TEST_F(TokenBucketRateLimiterTest, EdgeCaseMinimalRefillRate) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 1;
  test_config.max_tokens = 10;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(5));

  MockClock::advance_time(std::chrono::seconds(1));

  EXPECT_TRUE(limiter.allow(5));
}

TEST_F(TokenBucketRateLimiterTest, EdgeCaseMaximalRefillRate) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 1000000;
  test_config.max_tokens = 50;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(30));

  MockClock::advance_time(std::chrono::seconds(2));

  EXPECT_TRUE(limiter.allow(50));
  EXPECT_FALSE(limiter.allow(1));
}

TEST_F(TokenBucketRateLimiterTest, SequentialRequestsConsumeTokens) {
  TokenBucketRateLimiter<std::chrono::steady_clock> limiter(config);

  EXPECT_TRUE(limiter.allow(20));
  EXPECT_TRUE(limiter.allow(30));
  EXPECT_TRUE(limiter.allow(40));
  EXPECT_FALSE(limiter.allow(20));
}

TEST_F(TokenBucketRateLimiterTest, RefillCalculationPrecision) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 100;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(90));

  MockClock::advance_time(std::chrono::seconds(1));

  EXPECT_TRUE(limiter.allow(10));
  EXPECT_FALSE(limiter.allow(11));
}

// TEST_F(TokenBucketRateLimiterTest, BurstBehavior) {
//   TokenBucketConfig test_config;
//   test_config.refill_rate_seconds = 10;
//   test_config.max_tokens = 50;
//
//   TokenBucketRateLimiter<MockClock> limiter(test_config);
//
//   auto start_time = std::chrono::steady_clock::now();
//   MockClock::set_time(start_time);
//
//   // Initially can burst up to max_tokens (demonstrates burst capacity)
//   EXPECT_TRUE(limiter.allow(50));
//   EXPECT_FALSE(limiter.allow(1));
//
//   // After time passes, tokens refill allowing another burst
//   MockClock::advance_time(std::chrono::seconds(2));
//
//   // Should have accumulated 20 new tokens
//   EXPECT_TRUE(limiter.allow(20));
//   EXPECT_FALSE(limiter.allow(1));
// }

TEST_F(TokenBucketRateLimiterTest, MultipleRefillsAccumulate) {
  TokenBucketConfig test_config;
  test_config.refill_rate_seconds = 10;
  test_config.max_tokens = 50;

  TokenBucketRateLimiter<MockClock> limiter(test_config);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  EXPECT_TRUE(limiter.allow(40));

  MockClock::advance_time(std::chrono::seconds(1));

  EXPECT_TRUE(limiter.allow(10));
  EXPECT_FALSE(limiter.allow(6));
}

}  // namespace futility::rate_limiter