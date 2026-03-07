#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"

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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
  std::string key = "test_key";

  // Should allow all requests within the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key)) << "Request " << i << " should be allowed";
  }
}

TEST_F(SlidingWindowRateLimiterTest, BasicRateLimitingRejectsOverLimit) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
  std::string key = "test_key";

  // Fill up to the limit
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow(key));
  }

  // Next request should be rejected
  EXPECT_FALSE(limiter.allow(key)) << "Request over limit should be rejected";
}

TEST_F(SlidingWindowRateLimiterTest, SlidingWindowResetsAfterWindowSize) {
  SlidingWindowRateLimiterConfig config{max_requests, std::chrono::milliseconds(100), ttl,
                                        cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{max_requests, std::chrono::milliseconds(200), ttl,
                                        cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{10, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
  std::string key = "test_key";

  // Use cost of 3, should allow 3 requests (3*3 = 9 < 10)
  EXPECT_TRUE(limiter.allow(key, 3));
  EXPECT_TRUE(limiter.allow(key, 3));
  EXPECT_TRUE(limiter.allow(key, 3));

  // Next request should be rejected (9 + 3 = 12 > 10)
  EXPECT_FALSE(limiter.allow(key, 3));
}

TEST_F(SlidingWindowRateLimiterTest, CostParameterMixedCosts) {
  SlidingWindowRateLimiterConfig config{10, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
  std::string key = "test_key";

  EXPECT_TRUE(limiter.allow(key, 5));  // Total: 5
  EXPECT_TRUE(limiter.allow(key, 3));  // Total: 8
  EXPECT_TRUE(limiter.allow(key, 2));  // Total: 10

  // Should reject any positive cost now
  EXPECT_FALSE(limiter.allow(key, 1));
  EXPECT_FALSE(limiter.allow(key, 5));
}

TEST_F(SlidingWindowRateLimiterTest, CostParameterLargeSingleRequest) {
  SlidingWindowRateLimiterConfig config{10, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
  std::string key = "test_key";

  // Large single request that exceeds limit
  EXPECT_FALSE(limiter.allow(key, 15));

  // Should still allow smaller requests
  EXPECT_TRUE(limiter.allow(key, 5));
}

TEST_F(SlidingWindowRateLimiterTest, CleanupRemovesExpiredKeys) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(10)};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::seconds(10)};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{100, std::chrono::milliseconds(1000), ttl,
                                        cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{20, std::chrono::milliseconds(1000), ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);

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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);
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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<int> int_limiter(config);

  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(int_limiter.allow(42));
  }
  EXPECT_FALSE(int_limiter.allow(42));

  // Key 43 should be independent
  EXPECT_TRUE(int_limiter.allow(43));
}

TEST_F(SlidingWindowRateLimiterTest, WeightedCountCalculation) {
  // Use MockClock for predictable timing
  SlidingWindowRateLimiterConfig config{10, std::chrono::milliseconds(400), ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);
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
  SlidingWindowRateLimiterConfig config1{0, window_size, ttl, cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config1)), std::invalid_argument);
  SlidingWindowRateLimiterConfig config2{-1, window_size, ttl, cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config2)), std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidWindowSize) {
  SlidingWindowRateLimiterConfig config1{max_requests, std::chrono::milliseconds(0), ttl,
                                         cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config1)), std::invalid_argument);
  SlidingWindowRateLimiterConfig config2{max_requests, std::chrono::milliseconds(-1), ttl,
                                         cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config2)), std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidTTL) {
  SlidingWindowRateLimiterConfig config1{max_requests, window_size, std::chrono::milliseconds(0),
                                         cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config1)), std::invalid_argument);
  SlidingWindowRateLimiterConfig config2{max_requests, window_size, std::chrono::milliseconds(-1),
                                         cleanup_interval};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config2)), std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidCleanupInterval) {
  SlidingWindowRateLimiterConfig config1{max_requests, window_size, ttl,
                                         std::chrono::milliseconds(0)};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config1)), std::invalid_argument);
  SlidingWindowRateLimiterConfig config2{max_requests, window_size, ttl,
                                         std::chrono::milliseconds(-1)};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config2)), std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorBoundsCheckingInvalidMaxKeys) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval, 0};
  EXPECT_THROW((SlidingWindowRateLimiter<std::string>(config)), std::invalid_argument);
}

TEST_F(SlidingWindowRateLimiterTest, ConstructorValidParameters) {
  SlidingWindowRateLimiterConfig config1{max_requests, window_size, ttl, cleanup_interval};
  EXPECT_NO_THROW((SlidingWindowRateLimiter<std::string>(config1)));
  SlidingWindowRateLimiterConfig config2{max_requests, window_size, ttl, cleanup_interval, 10};
  EXPECT_NO_THROW((SlidingWindowRateLimiter<std::string>(config2)));
  SlidingWindowRateLimiterConfig config3{max_requests, window_size, ttl, cleanup_interval,
                                         std::nullopt};
  EXPECT_NO_THROW((SlidingWindowRateLimiter<std::string>(config3)));
}

TEST_F(SlidingWindowRateLimiterTest, MaxKeysLimitBasicFunctionality) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval, 2};
  SlidingWindowRateLimiter<std::string> limiter(config);

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
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);

  // Should allow many keys without limit
  for (int i = 0; i < 50; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)));
  }
}

TEST_F(SlidingWindowRateLimiterTest, MaxKeysLimitInteractsWithCleanup) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(10), 2};
  SlidingWindowRateLimiter<std::string> limiter(config);

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
  SlidingWindowRateLimiterConfig config{5, std::chrono::milliseconds(100), ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

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
  SlidingWindowRateLimiterConfig config{5, std::chrono::milliseconds(100), ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

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
  SlidingWindowRateLimiterConfig config{5, std::chrono::milliseconds(100), ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

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

// ============================================================================
// Eviction Tests with MockClock
// ============================================================================

TEST_F(SlidingWindowRateLimiterTest, EvictionRemovesExpiredKeysWithMockClock) {
  // TTL of 100ms, cleanup interval of 50ms
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(50)};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Access key1 and key2
  EXPECT_TRUE(limiter.allow("key1"));
  EXPECT_TRUE(limiter.allow("key2"));

  // Advance time past TTL and cleanup interval
  MockClock::advance_time(std::chrono::milliseconds(150));

  // Access key3, which should trigger cleanup and evict key1 and key2
  EXPECT_TRUE(limiter.allow("key3"));

  // key1 and key2 should be evicted and treated as fresh keys
  // (full quota available again)
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow("key1")) << "Request " << i << " to evicted key1 should be allowed";
  }
  EXPECT_FALSE(limiter.allow("key1"));
}

TEST_F(SlidingWindowRateLimiterTest, EvictionPreservesRecentlyAccessedKeys) {
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(50)};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Access key1 and use up its quota
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow("key1"));
  }
  EXPECT_FALSE(limiter.allow("key1"));

  // Advance time but not past TTL
  MockClock::advance_time(std::chrono::milliseconds(60));

  // Access key1 again to update its last_access time
  EXPECT_FALSE(limiter.allow("key1"));  // Still rate limited

  // Advance time past original TTL but not past key1's new last_access + TTL
  MockClock::advance_time(std::chrono::milliseconds(60));

  // Access key2 to trigger cleanup
  EXPECT_TRUE(limiter.allow("key2"));

  // key1 should still be rate limited (not evicted because we accessed it recently)
  EXPECT_FALSE(limiter.allow("key1"));
}

TEST_F(SlidingWindowRateLimiterTest, EvictionWithMaxKeysLimit) {
  // max_keys = 3, TTL = 100ms, cleanup = 50ms
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(50), 3};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Fill up to max keys
  EXPECT_TRUE(limiter.allow("key1"));
  EXPECT_TRUE(limiter.allow("key2"));
  EXPECT_TRUE(limiter.allow("key3"));
  EXPECT_FALSE(limiter.allow("key4"));  // Rejected due to max keys

  // Advance time past TTL for key1 and key2
  MockClock::advance_time(std::chrono::milliseconds(60));
  EXPECT_TRUE(limiter.allow("key3"));  // Refresh key3's last_access

  MockClock::advance_time(std::chrono::milliseconds(60));

  // This should trigger cleanup, evicting key1 and key2 but keeping key3
  EXPECT_TRUE(limiter.allow("key4"));  // Now allowed after eviction
  EXPECT_TRUE(limiter.allow("key5"));  // Also allowed
  EXPECT_FALSE(limiter.allow("key6"));  // Rejected - max keys again (key3, key4, key5)
}

// ============================================================================
// Cleanup Behavior Tests with MockClock
// ============================================================================

TEST_F(SlidingWindowRateLimiterTest, CleanupOnlyRunsAfterInterval) {
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(100)};  // cleanup_interval > ttl
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Access key1 once at t=0
  EXPECT_TRUE(limiter.allow("key1"));

  // Advance past TTL but before cleanup interval
  MockClock::advance_time(std::chrono::milliseconds(75));

  // Access key2 - should NOT trigger cleanup yet (75ms < 100ms cleanup_interval)
  EXPECT_TRUE(limiter.allow("key2"));

  // key1 should still exist at this point (cleanup hasn't run)
  // We can verify by accessing key1 again - it will refresh last_access but that's OK
  // since we're just confirming key1 wasn't evicted prematurely
  EXPECT_TRUE(limiter.allow("key1"));  // key1 still has quota (only 1 of 5 used)

  // Now advance more - key2 is at t=75ms, key1 is now also at t=75ms (just refreshed)
  // Advance to t=200ms (past cleanup_interval from t=0)
  MockClock::advance_time(std::chrono::milliseconds(125));

  // Access key3 - should trigger cleanup
  // cutoff = t=200 - 50(ttl) = t=150
  // key1 last_access = t=75 < t=150, so key1 should be evicted
  // key2 last_access = t=75 < t=150, so key2 should be evicted
  EXPECT_TRUE(limiter.allow("key3"));

  // key1 should now be evicted and fresh (full quota available)
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow("key1")) << "Request " << i << " should be allowed after eviction";
  }
  EXPECT_FALSE(limiter.allow("key1"));  // Now rate limited again
}

TEST_F(SlidingWindowRateLimiterTest, CleanupDoesNotEvictActiveKeys) {
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  // TTL = 100ms, cleanup_interval = 50ms
  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(100),
                                        std::chrono::milliseconds(50)};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Create keys at t=0: key0 through key4
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)));
  }

  // At t=60ms, access key0 and key1 to keep them active
  MockClock::advance_time(std::chrono::milliseconds(60));
  EXPECT_TRUE(limiter.allow("key0"));  // key0 last_access = t=60
  EXPECT_TRUE(limiter.allow("key1"));  // key1 last_access = t=60

  // At t=160ms, trigger cleanup
  // cutoff = t=160 - 100(ttl) = t=60
  // key0 last_access = t=60, NOT < t=60, so NOT evicted
  // key1 last_access = t=60, NOT < t=60, so NOT evicted
  // key2 last_access = t=0 < t=60, so evicted
  // key3 last_access = t=0 < t=60, so evicted
  // key4 last_access = t=0 < t=60, so evicted
  MockClock::advance_time(std::chrono::milliseconds(100));
  EXPECT_TRUE(limiter.allow("new_key"));  // Triggers cleanup

  // key0 and key1 should still have their counts (not evicted)
  // key0 had 2 requests (1 at t=0, 1 at t=60), so should allow 3 more
  int key0_remaining = 0;
  while (limiter.allow("key0")) {
    key0_remaining++;
  }
  EXPECT_EQ(key0_remaining, max_requests - 2) << "key0 should have 2 requests already used";

  // key2 should be evicted and have full quota
  for (int i = 0; i < max_requests; ++i) {
    EXPECT_TRUE(limiter.allow("key2")) << "Request " << i << " to evicted key2 should be allowed";
  }
  EXPECT_FALSE(limiter.allow("key2"));
}

TEST_F(SlidingWindowRateLimiterTest, CleanupHandlesEmptyMap) {
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(10)};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Advance time past cleanup interval without any keys
  MockClock::advance_time(std::chrono::milliseconds(100));

  // First access should work fine (cleanup on empty map is a no-op)
  EXPECT_TRUE(limiter.allow("key1"));
}

// ============================================================================
// High-Key Volume Tests
// ============================================================================

TEST_F(SlidingWindowRateLimiterTest, HighKeyVolumeWithoutLimit) {
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval};
  SlidingWindowRateLimiter<std::string> limiter(config);

  const int num_keys = 1000;

  // Create many keys
  for (int i = 0; i < num_keys; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)))
        << "First request for key " << i << " should be allowed";
  }

  // All keys should be independently rate limited
  for (int i = 0; i < num_keys; ++i) {
    std::string key = "key" + std::to_string(i);
    // Each key already has 1 request, should allow max_requests - 1 more
    for (int j = 1; j < max_requests; ++j) {
      EXPECT_TRUE(limiter.allow(key)) << "Request " << j << " for key " << i << " should be allowed";
    }
    EXPECT_FALSE(limiter.allow(key)) << "Request over limit for key " << i << " should be rejected";
  }
}

TEST_F(SlidingWindowRateLimiterTest, HighKeyVolumeWithMaxKeysLimit) {
  const size_t max_keys_limit = 100;
  SlidingWindowRateLimiterConfig config{max_requests, window_size, ttl, cleanup_interval,
                                        max_keys_limit};
  SlidingWindowRateLimiter<std::string> limiter(config);

  // Create keys up to the limit
  for (size_t i = 0; i < max_keys_limit; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)))
        << "Key " << i << " should be allowed (under limit)";
  }

  // Additional keys should be rejected
  for (int i = 0; i < 50; ++i) {
    EXPECT_FALSE(limiter.allow("extra_key" + std::to_string(i)))
        << "Extra key " << i << " should be rejected (over max keys)";
  }

  // Existing keys should still work
  for (size_t i = 0; i < max_keys_limit; ++i) {
    EXPECT_TRUE(limiter.allow("key" + std::to_string(i)))
        << "Existing key " << i << " should still be allowed";
  }
}

TEST_F(SlidingWindowRateLimiterTest, HighKeyVolumeWithEviction) {
  // Small TTL and cleanup interval for fast eviction
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  const size_t max_keys_limit = 50;
  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(10), max_keys_limit};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Fill up to max keys
  for (size_t i = 0; i < max_keys_limit; ++i) {
    EXPECT_TRUE(limiter.allow("batch1_key" + std::to_string(i)));
  }
  EXPECT_FALSE(limiter.allow("overflow_key"));

  // Advance time past TTL + cleanup interval
  MockClock::advance_time(std::chrono::milliseconds(100));

  // New keys should now be allowed after eviction
  for (size_t i = 0; i < max_keys_limit; ++i) {
    EXPECT_TRUE(limiter.allow("batch2_key" + std::to_string(i)))
        << "Batch2 key " << i << " should be allowed after eviction";
  }
  EXPECT_FALSE(limiter.allow("batch2_overflow"));
}

TEST_F(SlidingWindowRateLimiterTest, HighKeyVolumeConcurrentAccess) {
  const size_t max_keys_limit = 200;
  SlidingWindowRateLimiterConfig config{10, std::chrono::milliseconds(1000), ttl, cleanup_interval,
                                        max_keys_limit};
  SlidingWindowRateLimiter<std::string> limiter(config);

  const int num_threads = 20;
  const int keys_per_thread = 50;  // Each thread tries different keys
  std::vector<std::future<int>> futures;

  for (int t = 0; t < num_threads; ++t) {
    futures.push_back(std::async(std::launch::async, [&limiter, t]() {
      int successful_keys = 0;
      for (int i = 0; i < keys_per_thread; ++i) {
        std::string key = "thread" + std::to_string(t) + "_key" + std::to_string(i);
        if (limiter.allow(key)) {
          successful_keys++;
        }
      }
      return successful_keys;
    }));
  }

  int total_successful = 0;
  for (auto& future : futures) {
    total_successful += future.get();
  }

  // Should not exceed max_keys_limit total unique keys
  EXPECT_LE(total_successful, static_cast<int>(max_keys_limit))
      << "Total successful keys should not exceed max_keys_limit";
  EXPECT_GT(total_successful, 0) << "Should allow some keys";
}

TEST_F(SlidingWindowRateLimiterTest, HighKeyVolumeMemoryStability) {
  // Test that with eviction, memory usage stays bounded even with many unique keys over time
  auto start_time = std::chrono::steady_clock::now();
  MockClock::set_time(start_time);

  SlidingWindowRateLimiterConfig config{max_requests, window_size, std::chrono::milliseconds(50),
                                        std::chrono::milliseconds(10), 100};
  SlidingWindowRateLimiter<std::string, MockClock> limiter(config);

  // Simulate many batches of keys over time
  for (int batch = 0; batch < 10; ++batch) {
    // Each batch uses 100 unique keys
    for (int i = 0; i < 100; ++i) {
      std::string key = "batch" + std::to_string(batch) + "_key" + std::to_string(i);
      limiter.allow(key);  // May or may not succeed depending on max_keys
    }

    // Advance time to trigger eviction of previous batch
    MockClock::advance_time(std::chrono::milliseconds(100));
  }

  // After all batches, should still be able to add new keys
  // (old ones should have been evicted)
  int new_keys_allowed = 0;
  for (int i = 0; i < 100; ++i) {
    if (limiter.allow("final_key" + std::to_string(i))) {
      new_keys_allowed++;
    }
  }

  EXPECT_EQ(new_keys_allowed, 100) << "Should allow full batch of new keys after evictions";
}

}  // namespace futility::rate_limiter