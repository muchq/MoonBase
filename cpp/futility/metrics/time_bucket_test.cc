#include "cpp/futility/metrics/time_bucket.h"
#include "cpp/futility/metrics/mock_clock.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>

namespace futility::metrics {

class TimeBucketTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockClock::set_time(std::chrono::steady_clock::now());
  }
};

TEST_F(TimeBucketTest, BasicAddAndRetrieve) {
  TimeBucket<int> bucket;
  
  bucket.add_value(42);
  bucket.add_value(84);
  
  EXPECT_EQ(bucket.get_count(), 2);
  
  auto values = bucket.get_values();
  EXPECT_EQ(values.size(), 2);
  EXPECT_EQ(values[0], 42);
  EXPECT_EQ(values[1], 84);
}

TEST_F(TimeBucketTest, IsExpiredBasedOnTimestamp) {
  auto now = MockClock::now();
  TimeBucket<int> bucket(now);
  
  // Not expired when checked at creation time
  EXPECT_FALSE(bucket.is_expired(now, std::chrono::minutes(5)));
  
  // Still not expired after 4 minutes
  MockClock::advance_time(std::chrono::minutes(4));
  EXPECT_FALSE(bucket.is_expired(MockClock::now(), std::chrono::minutes(5)));
  
  // Expired after 6 minutes
  MockClock::advance_time(std::chrono::minutes(2));
  EXPECT_TRUE(bucket.is_expired(MockClock::now(), std::chrono::minutes(5)));
}

TEST_F(TimeBucketTest, ResetClearsBucket) {
  TimeBucket<int> bucket;
  
  bucket.add_value(42);
  bucket.add_value(84);
  EXPECT_EQ(bucket.get_count(), 2);
  
  bucket.reset();
  EXPECT_EQ(bucket.get_count(), 0);
  EXPECT_TRUE(bucket.get_values().empty());
}

TEST_F(TimeBucketTest, ConcurrentAccess) {
  TimeBucket<int> bucket;
  const int num_threads = 10;
  const int values_per_thread = 100;
  
  std::vector<std::thread> threads;
  
  // Spawn threads that add values concurrently
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&bucket, i]() {
      for (int j = 0; j < values_per_thread; ++j) {
        bucket.add_value(i * values_per_thread + j);
      }
    });
  }
  
  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  // Verify total count
  EXPECT_EQ(bucket.get_count(), num_threads * values_per_thread);
  
  // Verify we can read values without crashing
  auto values = bucket.get_values();
  EXPECT_EQ(values.size(), num_threads * values_per_thread);
}

TEST_F(TimeBucketTest, EstimatedMemoryUsage) {
  TimeBucket<int> bucket;
  
  size_t initial_memory = bucket.estimated_memory_usage();
  
  // Add some values
  for (int i = 0; i < 100; ++i) {
    bucket.add_value(i);
  }
  
  size_t memory_with_values = bucket.estimated_memory_usage();
  
  // Memory usage should increase
  EXPECT_GT(memory_with_values, initial_memory);
}

TEST_F(TimeBucketTest, DifferentValueTypes) {
  // Test with different value types
  TimeBucket<std::chrono::microseconds> duration_bucket;
  TimeBucket<double> double_bucket;
  TimeBucket<std::string> string_bucket;
  
  duration_bucket.add_value(std::chrono::microseconds(1000));
  double_bucket.add_value(3.14);
  string_bucket.add_value("test_string");
  
  EXPECT_EQ(duration_bucket.get_count(), 1);
  EXPECT_EQ(double_bucket.get_count(), 1);
  EXPECT_EQ(string_bucket.get_count(), 1);
  
  auto duration_values = duration_bucket.get_values();
  auto double_values = double_bucket.get_values();
  auto string_values = string_bucket.get_values();
  
  EXPECT_EQ(duration_values[0], std::chrono::microseconds(1000));
  EXPECT_DOUBLE_EQ(double_values[0], 3.14);
  EXPECT_EQ(string_values[0], "test_string");
}

}  // namespace futility::metrics