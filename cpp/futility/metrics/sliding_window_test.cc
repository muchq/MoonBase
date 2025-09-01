#include "cpp/futility/metrics/sliding_window.h"
#include "cpp/futility/metrics/mock_clock.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>

namespace futility::metrics {

class SlidingWindowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockClock::reset();
    MockClock::set_time(std::chrono::steady_clock::now());
  }
};

TEST_F(SlidingWindowTest, BasicRecordAndRetrieve) {
  MetricsConfig config;
  config.bucket_size = std::chrono::minutes(1);
  config.retention_period = std::chrono::minutes(5);
  
  SlidingWindow<int, MockClock> window(config);
  
  window.record(42);
  window.record(84);
  
  auto values = window.get_values_in_window();
  EXPECT_EQ(values.size(), 2);
  EXPECT_EQ(window.total_sample_count(), 2);
}

TEST_F(SlidingWindowTest, BucketEviction) {
  MetricsConfig config;
  config.bucket_size = std::chrono::minutes(1);
  config.retention_period = std::chrono::minutes(5);
  
  SlidingWindow<int, MockClock> window(config);
  
  auto start_time = MockClock::now();
  MockClock::set_time(start_time);
  
  // Record some data
  window.record(10);
  EXPECT_EQ(window.bucket_count(), 1);
  
  // Advance time beyond retention period
  MockClock::advance_time(std::chrono::minutes(6));
  
  // Record new data (should trigger cleanup)
  window.record(20);
  
  auto values = window.get_values_in_window();
  // Should only have the new value, old bucket evicted
  EXPECT_EQ(values.size(), 1);
  EXPECT_EQ(values[0], 20);
}

TEST_F(SlidingWindowTest, MultipleBuckets) {
  MetricsConfig config;
  config.bucket_size = std::chrono::minutes(1);
  config.retention_period = std::chrono::minutes(5);
  
  SlidingWindow<int, MockClock> window(config);
  
  // Record data in first bucket
  window.record(1);
  EXPECT_EQ(window.bucket_count(), 1);
  
  // Advance time to create new bucket
  MockClock::advance_time(std::chrono::minutes(1) + std::chrono::seconds(1));
  window.record(2);
  EXPECT_EQ(window.bucket_count(), 2);
  
  // Advance time to create third bucket
  MockClock::advance_time(std::chrono::minutes(1) + std::chrono::seconds(1));
  window.record(3);
  EXPECT_EQ(window.bucket_count(), 3);
  
  auto values = window.get_values_in_window();
  EXPECT_EQ(values.size(), 3);
  EXPECT_EQ(window.total_sample_count(), 3);
}

TEST_F(SlidingWindowTest, ManualCleanup) {
  MetricsConfig config;
  config.bucket_size = std::chrono::minutes(1);
  config.retention_period = std::chrono::minutes(5);
  
  SlidingWindow<int, MockClock> window(config);
  
  // Record some data
  window.record(10);
  EXPECT_EQ(window.bucket_count(), 1);
  
  // Advance time beyond retention period
  MockClock::advance_time(std::chrono::minutes(6));
  
  // Manual cleanup
  window.cleanup_expired_buckets();
  
  // Should have no buckets left
  EXPECT_EQ(window.bucket_count(), 0);
  EXPECT_TRUE(window.get_values_in_window().empty());
}

TEST_F(SlidingWindowTest, ConcurrentAccess) {
  MetricsConfig config;
  config.bucket_size = std::chrono::seconds(1);
  config.retention_period = std::chrono::minutes(1);
  
  SlidingWindow<int, MockClock> window(config);
  
  const int num_threads = 10;
  const int values_per_thread = 100;
  std::vector<std::thread> threads;
  
  // Spawn threads that record values concurrently
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&window, i]() {
      for (int j = 0; j < values_per_thread; ++j) {
        window.record(i * values_per_thread + j);
      }
    });
  }
  
  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  // Verify total count
  EXPECT_EQ(window.total_sample_count(), num_threads * values_per_thread);
  
  // Verify we can read values without crashing
  auto values = window.get_values_in_window();
  EXPECT_EQ(values.size(), num_threads * values_per_thread);
}

TEST_F(SlidingWindowTest, MemoryBounds) {
  MetricsConfig config;
  config.bucket_size = std::chrono::minutes(1);
  config.retention_period = std::chrono::minutes(5);
  
  SlidingWindow<int, MockClock> window(config);
  
  size_t initial_memory = window.estimated_memory_usage();
  
  // Add data across multiple buckets
  for (int bucket = 0; bucket < 3; ++bucket) {
    for (int i = 0; i < 100; ++i) {
      window.record(i);
    }
    MockClock::advance_time(std::chrono::minutes(1) + std::chrono::seconds(1));
  }
  
  size_t memory_with_data = window.estimated_memory_usage();
  EXPECT_GT(memory_with_data, initial_memory);
  
  // Advance time to expire all buckets
  MockClock::advance_time(config.retention_period + std::chrono::minutes(1));
  window.cleanup_expired_buckets();
  
  size_t memory_after_cleanup = window.estimated_memory_usage();
  // Memory should be back to roughly initial size
  EXPECT_LT(memory_after_cleanup, memory_with_data);
}

TEST_F(SlidingWindowTest, DifferentValueTypes) {
  MetricsConfig config;
  
  SlidingWindow<std::chrono::microseconds, MockClock> duration_window(config);
  SlidingWindow<double, MockClock> double_window(config);
  SlidingWindow<bool, MockClock> bool_window(config);
  
  duration_window.record(std::chrono::microseconds(1000));
  double_window.record(3.14);
  bool_window.record(true);
  bool_window.record(false);
  
  EXPECT_EQ(duration_window.total_sample_count(), 1);
  EXPECT_EQ(double_window.total_sample_count(), 1);
  EXPECT_EQ(bool_window.total_sample_count(), 2);
  
  auto duration_values = duration_window.get_values_in_window();
  auto double_values = double_window.get_values_in_window();
  auto bool_values = bool_window.get_values_in_window();
  
  EXPECT_EQ(duration_values[0], std::chrono::microseconds(1000));
  EXPECT_DOUBLE_EQ(double_values[0], 3.14);
  EXPECT_EQ(bool_values.size(), 2);
}

TEST_F(SlidingWindowTest, EmptyWindow) {
  SlidingWindow<int, MockClock> window;
  
  EXPECT_EQ(window.bucket_count(), 0);
  EXPECT_EQ(window.total_sample_count(), 0);
  EXPECT_TRUE(window.get_values_in_window().empty());
  
  // Cleanup on empty window should not crash
  window.cleanup_expired_buckets();
  EXPECT_EQ(window.bucket_count(), 0);
}

}  // namespace futility::metrics