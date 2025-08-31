#include "cpp/futility/rate_limiter/sliding_window_rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace futility::rate_limiter {

// MockClock for predictable testing
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

class SlidingWindowRateLimiterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Default test configuration
    max_requests = 5;
    window_size = std::chrono::milliseconds(1000);
    ttl = std::chrono::milliseconds(5000);
    cleanup_interval = std::chrono::milliseconds(100);
  }

  long max_requests;
  std::chrono::milliseconds window_size;
  std::chrono::milliseconds ttl;
  std::chrono::milliseconds cleanup_interval;
};

TEST_F(SlidingWindowRateLimiterTest, BasicRateLimitingAllowsWithinLimit) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Should allow all requests within the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key)) << "Request " << i << " should be allowed";
  }
}

TEST_F(SlidingWindowRateLimiterTest, BasicRateLimitingRejectsOverLimit) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Fill up to the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // Next request should be rejected
  EXPECT_FALSE(limiter.allow(key)) << "Request over limit should be rejected";
}

TEST_F(SlidingWindowRateLimiterTest, SlidingWindowResetsAfterWindowSize) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, std::chrono::milliseconds(100), ttl,
                                                cleanup_interval);
  std::string key = "test_key";

  // Fill up to the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // Should be rejected immediately
  EXPECT_FALSE(limiter.allow(key));

  // Wait for window to slide
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Should allow again after window slides
  EXPECT_TRUE(limiter.allow(key)) << "Should allow after window slides";
}

TEST_F(SlidingWindowRateLimiterTest, SlidingWindowGradualTransition) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, std::chrono::milliseconds(200), ttl,
                                                cleanup_interval);
  std::string key = "test_key";

  // Fill up to the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // Should be rejected
  EXPECT_FALSE(limiter.allow(key));

  // Wait for partial window slide (half way through)
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should still be rejected due to weighted count
  EXPECT_FALSE(limiter.allow(key)) << "Should still reject during partial window slide";

  // Wait for full window slide
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Should allow again
  EXPECT_TRUE(limiter.allow(key)) << "Should allow after full window slide";
}

TEST_F(SlidingWindowRateLimiterTest, MultipleKeysIndependent) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);
  std::string key1 = "key1";
  std::string key2 = "key2";

  // Fill up key1 to the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key1));
  }

  // key1 should be rejected
  EXPECT_FALSE(limiter.allow(key1));

  // key2 should still be allowed (independent limit)
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key2)) << "Key2 request " << i << " should be allowed";
  }

  // key2 should now be rejected too
  EXPECT_FALSE(limiter.allow(key2));
}

TEST_F(SlidingWindowRateLimiterTest, CostParameterBasicFunctionality) {
  SlidingWindowRateLimiter<std::string> limiter(10, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Use cost of 3, should allow 3 requests (3*3 = 9 < 10)
  EXPECT_TRUE(limiter.allow(key, 3));
  EXPECT_TRUE(limiter.allow(key, 3));
  EXPECT_TRUE(limiter.allow(key, 3));

  // Next request should be rejected (9 + 3 = 12 > 10)
  EXPECT_FALSE(limiter.allow(key, 3));
}

TEST_F(SlidingWindowRateLimiterTest, CostParameterMixedCosts) {
  SlidingWindowRateLimiter<std::string> limiter(10, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  EXPECT_TRUE(limiter.allow(key, 5));  // Total: 5
  EXPECT_TRUE(limiter.allow(key, 3));  // Total: 8
  EXPECT_TRUE(limiter.allow(key, 2));  // Total: 10

  // Should reject any positive cost now
  EXPECT_FALSE(limiter.allow(key, 1));
  EXPECT_FALSE(limiter.allow(key, 5));
}

TEST_F(SlidingWindowRateLimiterTest, CostParameterLargeSingleRequest) {
  SlidingWindowRateLimiter<std::string> limiter(10, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Large single request that exceeds limit
  EXPECT_FALSE(limiter.allow(key, 15));

  // Should still allow smaller requests
  EXPECT_TRUE(limiter.allow(key, 5));
}

TEST_F(SlidingWindowRateLimiterTest, CleanupRemovesExpiredKeys) {
  SlidingWindowRateLimiter<std::string> limiter(
      max_requests, window_size,
      std::chrono::milliseconds(50),   // Short TTL
      std::chrono::milliseconds(10));  // Short cleanup interval
  std::string key = "test_key";

  // Make some requests
  EXPECT_TRUE(limiter.allow(key));
  EXPECT_TRUE(limiter.allow(key));

  // Wait for TTL to expire and cleanup to run
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Trigger cleanup by accessing with same key
  EXPECT_TRUE(limiter.allow(key));

  // Key should have been cleaned up and reset, so we should be able to make max_requests again
  for (int i = 1; i < max_requests; ++i) {  // Start from 1 since we already made one request above
    EXPECT_TRUE(limiter.allow(key)) << "Request " << i << " should be allowed after cleanup";
  }
}

TEST_F(SlidingWindowRateLimiterTest, CleanupRespectCleanupInterval) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size,
                                                std::chrono::milliseconds(50),  // Short TTL
                                                std::chrono::seconds(10));  // Long cleanup interval
  std::string key1 = "key1";
  std::string key2 = "key2";

  // Access key1
  EXPECT_TRUE(limiter.allow(key1));

  // Wait for TTL to expire but not cleanup interval
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Access key2 - should not trigger cleanup of key1 yet
  EXPECT_TRUE(limiter.allow(key2));

  // The limiter should still remember key1 (cleanup hasn't run)
  // This is harder to test directly, but we can verify behavior is consistent
}

TEST_F(SlidingWindowRateLimiterTest, ConcurrentAccess) {
  SlidingWindowRateLimiter<std::string> limiter(100, std::chrono::milliseconds(1000), ttl,
                                                cleanup_interval);
  std::string key = "concurrent_key";

  const int num_threads = 10;
  const int requests_per_thread = 15;
  std::vector<std::future<int>> futures;

  // Launch multiple threads making concurrent requests
  for (int t = 0; t < num_threads; ++t) {
    futures.push_back(std::async(std::launch::async, [&limiter, &key]() {
      int allowed_count = 0;
      for (int i = 0; i < requests_per_thread; ++i) {
        if (limiter.allow(key)) {
          ++allowed_count;
        }
      }
      return allowed_count;
    }));
  }

  // Collect results
  int total_allowed = 0;
  for (auto& future : futures) {
    total_allowed += future.get();
  }

  // Should not exceed the limit even with concurrent access
  EXPECT_LE(total_allowed, 100) << "Concurrent access should not exceed rate limit";
  EXPECT_GT(total_allowed, 0) << "Should allow some requests";
}

TEST_F(SlidingWindowRateLimiterTest, ConcurrentAccessMultipleKeys) {
  SlidingWindowRateLimiter<std::string> limiter(20, std::chrono::milliseconds(1000), ttl,
                                                cleanup_interval);

  const int num_keys = 5;
  const int num_threads_per_key = 4;
  const int requests_per_thread = 10;

  std::vector<std::future<std::pair<std::string, int>>> futures;

  // Launch threads for each key
  for (int k = 0; k < num_keys; ++k) {
    std::string key = "key_" + std::to_string(k);

    for (int t = 0; t < num_threads_per_key; ++t) {
      futures.push_back(std::async(std::launch::async, [&limiter, key]() {
        int allowed_count = 0;
        for (int i = 0; i < requests_per_thread; ++i) {
          if (limiter.allow(key)) {
            ++allowed_count;
          }
        }
        return std::make_pair(key, allowed_count);
      }));
    }
  }

  // Collect results per key
  std::map<std::string, int> results_per_key;
  for (auto& future : futures) {
    auto [key, count] = future.get();
    results_per_key[key] += count;
  }

  // Each key should not exceed its individual limit
  for (const auto& [key, total_allowed] : results_per_key) {
    EXPECT_LE(total_allowed, 20) << "Key " << key << " should not exceed rate limit";
    EXPECT_GT(total_allowed, 0) << "Key " << key << " should allow some requests";
  }
}

TEST_F(SlidingWindowRateLimiterTest, EdgeCaseZeroCost) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Zero cost should always be allowed and not consume quota
  EXPECT_TRUE(limiter.allow(key, 0));
  EXPECT_TRUE(limiter.allow(key, 0));

  // Should still be able to make normal requests
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key, 1));
  }

  // Should be rejected now
  EXPECT_FALSE(limiter.allow(key, 1));
}

TEST_F(SlidingWindowRateLimiterTest, EdgeCaseNegativeCost) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);
  std::string key = "test_key";

  // Fill up to limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key, 1));
  }

  // Should be rejected
  EXPECT_FALSE(limiter.allow(key, 1));

  // Negative cost should reduce the count (though this might not be intended behavior)
  EXPECT_TRUE(limiter.allow(key, -2));

  // Should now allow more requests
  EXPECT_TRUE(limiter.allow(key, 1));
  EXPECT_TRUE(limiter.allow(key, 1));
}

TEST_F(SlidingWindowRateLimiterTest, DifferentKeyTypes) {
  // Test with integer keys
  SlidingWindowRateLimiter<int> int_limiter(max_requests, window_size, ttl, cleanup_interval);

  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(int_limiter.allow(42));
  }
  EXPECT_FALSE(int_limiter.allow(42));

  // Key 43 should be independent
  EXPECT_TRUE(int_limiter.allow(43));
}

TEST_F(SlidingWindowRateLimiterTest, WeightedCountCalculation) {
  // Use MockClock for predictable timing
  SlidingWindowRateLimiter<std::string, MockClock> limiter(10, std::chrono::milliseconds(400), ttl,
                                                           cleanup_interval);
  std::string key = "test_key";

  // Reset mock clock to a known time
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  // Make 5 requests in first window
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // Advance time by 100ms (25% of 400ms window)
  MockClock::advance_time(std::chrono::milliseconds(100));

  // Make 3 more requests in current window
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // At this point: window hasn't slid yet, so previous_count=0, current_count=8
  // elapsed_ratio = 100/400 = 0.25
  // weighted = 0 * (1-0.25) + 8 = 0 + 8 = 8

  // Should allow cost 2 request (8 + 2 = 10 <= 10)
  EXPECT_TRUE(limiter.allow(key, 2));

  // Now current_count = 10, weighted = 0 * 0.75 + 10 = 10
  // Should reject any positive cost (10 + 1 > 10)
  EXPECT_FALSE(limiter.allow(key, 1));

  // Advance time to trigger window slide (full 400ms)
  MockClock::advance_time(std::chrono::milliseconds(300));  // Total 400ms from start

  // Now window should slide: previous_count=10, current_count=0
  // elapsed_ratio = 0 (just slid)
  // weighted = 10 * (1-0) + 0 = 10
  EXPECT_FALSE(limiter.allow(key, 1));  // Still at limit

  // Advance time to 25% into new window
  MockClock::advance_time(std::chrono::milliseconds(100));  // 100ms into new window

  // weighted = 10 * (1-0.25) + 0 = 7.5
  EXPECT_TRUE(limiter.allow(key, 2));   // 7.5 + 2 = 9.5 < 10
  EXPECT_FALSE(limiter.allow(key, 1));  // 7.5 + 2 + 1 = 10.5 > 10
}

// Tests for new functionality added by improvements

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidMaxRequests) {
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(0, window_size, ttl, cleanup_interval),
               std::invalid_argument);
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(-1, window_size, ttl, cleanup_interval),
               std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidWindowSize) {
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(max_requests, std::chrono::milliseconds(0),
                                                     ttl, cleanup_interval),
               std::invalid_argument);
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(max_requests, std::chrono::milliseconds(-1),
                                                     ttl, cleanup_interval),
               std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidTTL) {
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(
                   max_requests, window_size, std::chrono::milliseconds(0), cleanup_interval),
               std::invalid_argument);
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(
                   max_requests, window_size, std::chrono::milliseconds(-1), cleanup_interval),
               std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidCleanupInterval) {
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl,
                                                     std::chrono::milliseconds(0)),
               std::invalid_argument);
  EXPECT_THROW(SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl,
                                                     std::chrono::milliseconds(-1)),
               std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidMaxKeys) {
  EXPECT_THROW(
      SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl, cleanup_interval, 0),
      std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorValidParameters) {
  EXPECT_NO_THROW(
      SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl, cleanup_interval));
  EXPECT_NO_THROW(
      SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl, cleanup_interval, 10));
  EXPECT_NO_THROW(SlidingWindowRateLimiter<std::string>(max_requests, window_size, ttl,
                                                        cleanup_interval, std::nullopt));
}

TEST_F(SlidingWindowRateLimiterTest, MaxKeysLimitBasicFunctionality) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval,
                                                2);

  // First two keys should work
  EXPECT_TRUE(limiter.allow("key1"));
  EXPECT_TRUE(limiter.allow("key2"));

  // Third key should be rejected due to max keys limit
  EXPECT_FALSE(limiter.allow("key3"));

  // Existing keys should still work
  EXPECT_TRUE(limiter.allow("key1"));
  EXPECT_TRUE(limiter.allow("key2"));
}

TEST_F(SlidingWindowRateLimiterTest, MaxKeysLimitWithNoLimit) {
  SlidingWindowRateLimiter<std::string> limiter(max_requests, window_size, ttl, cleanup_interval);

  // Should allow many keys without limit
  for (int i = 0; i < 50; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)));
  }
}

TEST_F(SlidingWindowRateLimiterTest, MaxKeysLimitInteractsWithCleanup) {
  SlidingWindowRateLimiter<std::string> limiter(
      max_requests, window_size,
      std::chrono::milliseconds(50),  // Short TTL
      std::chrono::milliseconds(10),  // Short cleanup interval
      2);                             // Max 2 keys

  // Fill up to max keys
  EXPECT_TRUE(limiter.allow("key1"));
  EXPECT_TRUE(limiter.allow("key2"));
  EXPECT_FALSE(limiter.allow("key3"));

  // Wait for keys to expire and cleanup to run
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should be able to add new keys after cleanup
  EXPECT_TRUE(limiter.allow("key3"));
  EXPECT_TRUE(limiter.allow("key4"));
  EXPECT_FALSE(limiter.allow("key5"));
}

TEST_F(SlidingWindowRateLimiterTest, WindowSlidingLongIdlePeriodMockClock) {
  SlidingWindowRateLimiter<std::string, MockClock> limiter(5, std::chrono::milliseconds(100), ttl,
                                                           cleanup_interval);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  // Use up the limit
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key1"));
  }
  EXPECT_FALSE(limiter.allow("key1"));

  // Advance time by more than 2 windows (250ms > 2 * 100ms)
  MockClock::advance_time(std::chrono::milliseconds(250));

  // Should reset both windows, allowing full capacity again
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key1"))
        << "Request " << i << " should be allowed after long idle period";
  }
  EXPECT_FALSE(limiter.allow("key1"));
}

TEST_F(SlidingWindowRateLimiterTest, WindowSlidingNormalSlideMockClock) {
  SlidingWindowRateLimiter<std::string, MockClock> limiter(5, std::chrono::milliseconds(100), ttl,
                                                           cleanup_interval);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  // Use up the limit
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key1"));
  }
  EXPECT_FALSE(limiter.allow("key1"));

  // Advance time by exactly one window (normal slide)
  MockClock::advance_time(std::chrono::milliseconds(100));

  // Should have some capacity due to sliding window algorithm
  // previous_count = 5, current_count = 0, elapsed_ratio = 0
  // weighted = 5 * (1-0) + 0 = 5, so no room for new requests initially
  EXPECT_FALSE(limiter.allow("key1"));

  // Advance time slightly into the new window
  MockClock::advance_time(std::chrono::milliseconds(20));  // 20% into new window

  // weighted = 5 * (1-0.2) + 0 = 4, so should allow 1 request
  EXPECT_TRUE(limiter.allow("key1"));
}

TEST_F(SlidingWindowRateLimiterTest, WindowSlidingExactBoundaries) {
  SlidingWindowRateLimiter<std::string, MockClock> limiter(5, std::chrono::milliseconds(100), ttl,
                                                           cleanup_interval);

  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  // Use up limit
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key1"));
  }
  EXPECT_FALSE(limiter.allow("key1"));

  // Test exactly at window boundary
  MockClock::advance_time(std::chrono::milliseconds(100));
  EXPECT_FALSE(limiter.allow("key1"));  // Still limited by previous window

  // Test exactly at 2x window boundary
  MockClock::advance_time(std::chrono::milliseconds(100));  // Total 200ms
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key1"))
        << "Should allow request " << i << " after 2x window boundary";
  }
  EXPECT_FALSE(limiter.allow("key1"));
}

}  // namespace futility::rate_limiter