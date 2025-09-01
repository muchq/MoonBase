#include "cpp/futility/metrics/metrics_service.h"
#include "cpp/futility/metrics/mock_clock.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>

namespace futility::metrics {

class MetricsServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockClock::reset();
    MockClock::set_time(std::chrono::steady_clock::now());
  }
};

TEST_F(MetricsServiceTest, BasicLatencyRecording) {
  MetricsService<MockClock> metrics;
  
  metrics.record_latency("test_latency", std::chrono::microseconds(100));
  metrics.record_latency("test_latency", std::chrono::microseconds(200));
  metrics.record_latency("test_latency", std::chrono::microseconds(300));
  
  auto reports = metrics.get_latency_reports();
  ASSERT_EQ(reports.size(), 1);
  
  const auto& report = reports[0];
  EXPECT_EQ(report.metric_name, "test_latency");
  EXPECT_EQ(report.sample_count, 3);
  EXPECT_GT(report.mean_microseconds, 100);
  EXPECT_LT(report.mean_microseconds, 300);
  EXPECT_GT(report.p50_microseconds, 100);
  EXPECT_LT(report.p99_microseconds, 400);
}

TEST_F(MetricsServiceTest, CounterMetrics) {
  MetricsService<MockClock> metrics;
  
  metrics.record_counter("test_counter", 5);
  metrics.record_counter("test_counter", 10);
  metrics.record_counter("test_counter", 15);
  
  auto reports = metrics.get_counter_reports();
  ASSERT_EQ(reports.size(), 1);
  
  const auto& report = reports[0];
  EXPECT_EQ(report.metric_name, "test_counter");
  EXPECT_EQ(report.total_count, 30);
  EXPECT_GT(report.rate_per_second, 0);
}

TEST_F(MetricsServiceTest, CacheMetrics) {
  MetricsService<MockClock> metrics;
  
  metrics.record_cache_event("test_cache", true);   // hit
  metrics.record_cache_event("test_cache", false);  // miss
  metrics.record_cache_event("test_cache", true);   // hit
  metrics.record_cache_event("test_cache", true);   // hit
  
  auto reports = metrics.get_cache_reports();
  ASSERT_EQ(reports.size(), 1);
  
  const auto& report = reports[0];
  EXPECT_EQ(report.metric_name, "test_cache");
  EXPECT_EQ(report.total_requests, 4);
  EXPECT_EQ(report.hit_count, 3);
  EXPECT_DOUBLE_EQ(report.hit_rate, 0.75);
}

TEST_F(MetricsServiceTest, MultipleMetricTypes) {
  MetricsService<MockClock> metrics;
  
  metrics.record_latency("api_latency", std::chrono::microseconds(1000));
  metrics.record_counter("api_requests", 1);
  metrics.record_cache_event("api_cache", true);
  metrics.record_gauge("cpu_usage", 85.5);
  
  EXPECT_EQ(metrics.get_metric_count(), 4);
  
  auto latency_reports = metrics.get_latency_reports();
  auto counter_reports = metrics.get_counter_reports();
  auto cache_reports = metrics.get_cache_reports();
  
  EXPECT_EQ(latency_reports.size(), 1);
  EXPECT_EQ(counter_reports.size(), 2);  // counter + gauge (treated as counter)
  EXPECT_EQ(cache_reports.size(), 1);
}

TEST_F(MetricsServiceTest, SlidingWindowExpiration) {
  MetricsConfig config;
  config.retention_period = std::chrono::minutes(5);
  config.bucket_size = std::chrono::minutes(1);
  
  MetricsService<MockClock> metrics(config);
  
  auto start_time = MockClock::now();
  MockClock::set_time(start_time);
  
  // Record some data
  metrics.record_counter("test_counter", 10);
  
  auto initial_reports = metrics.get_counter_reports();
  EXPECT_EQ(initial_reports.size(), 1);
  EXPECT_EQ(initial_reports[0].total_count, 10);
  
  // Advance time beyond retention period
  MockClock::advance_time(std::chrono::minutes(6));
  
  // Trigger cleanup by recording new data
  metrics.record_counter("test_counter", 5);
  
  auto reports_after_expiry = metrics.get_counter_reports();
  EXPECT_EQ(reports_after_expiry.size(), 1);
  EXPECT_EQ(reports_after_expiry[0].total_count, 5);  // Only new data
}

TEST_F(MetricsServiceTest, ManualCleanup) {
  MetricsConfig config;
  config.retention_period = std::chrono::minutes(5);
  
  MetricsService<MockClock> metrics(config);
  
  metrics.record_counter("test_counter", 10);
  EXPECT_EQ(metrics.get_counter_reports().size(), 1);
  
  // Advance time beyond retention
  MockClock::advance_time(std::chrono::minutes(6));
  
  // Manual cleanup
  metrics.cleanup_expired_data();
  
  auto reports = metrics.get_counter_reports();
  EXPECT_TRUE(reports.empty() || reports[0].total_count == 0);
}

TEST_F(MetricsServiceTest, ConcurrentRecording) {
  MetricsService<MockClock> metrics;
  const int num_threads = 5;
  const int records_per_thread = 20;
  
  std::vector<std::thread> threads;
  
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&metrics, i]() {
      for (int j = 0; j < records_per_thread; ++j) {
        metrics.record_latency("concurrent_test", std::chrono::microseconds((i + 1) * 100 + j));
        metrics.record_counter("concurrent_counter", 1);
        metrics.record_cache_event("concurrent_cache", j % 2 == 0);
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  auto latency_reports = metrics.get_latency_reports();
  auto counter_reports = metrics.get_counter_reports();
  auto cache_reports = metrics.get_cache_reports();
  
  ASSERT_EQ(latency_reports.size(), 1);
  ASSERT_EQ(counter_reports.size(), 1);
  ASSERT_EQ(cache_reports.size(), 1);
  
  EXPECT_EQ(latency_reports[0].sample_count, num_threads * records_per_thread);
  EXPECT_EQ(counter_reports[0].total_count, num_threads * records_per_thread);
  EXPECT_EQ(cache_reports[0].total_requests, num_threads * records_per_thread);
}

TEST_F(MetricsServiceTest, MemoryBounds) {
  MetricsConfig config;
  config.max_metrics = 5;
  
  MetricsService<MockClock> metrics(config);
  
  // Add metrics up to limit
  for (int i = 0; i < 5; ++i) {
    metrics.record_counter("counter_" + std::to_string(i), 1);
  }
  
  EXPECT_EQ(metrics.get_metric_count(), 5);
  
  // Try to add beyond limit
  metrics.record_counter("counter_overflow", 1);
  
  // Should still be at limit
  EXPECT_EQ(metrics.get_metric_count(), 5);
  
  // The overflow metric should not appear in reports
  auto reports = metrics.get_counter_reports();
  bool found_overflow = false;
  for (const auto& report : reports) {
    if (report.metric_name == "counter_overflow") {
      found_overflow = true;
      break;
    }
  }
  EXPECT_FALSE(found_overflow);
}

TEST_F(MetricsServiceTest, EmptyReports) {
  MetricsService<MockClock> metrics;
  
  // No data recorded
  EXPECT_TRUE(metrics.get_latency_reports().empty());
  EXPECT_TRUE(metrics.get_counter_reports().empty());
  EXPECT_TRUE(metrics.get_cache_reports().empty());
  EXPECT_EQ(metrics.get_metric_count(), 0);
}

TEST_F(MetricsServiceTest, DefaultCounter) {
  MetricsService<MockClock> metrics;
  
  // Test default counter value
  metrics.record_counter("default_test");  // Should default to 1
  metrics.record_counter("default_test");
  
  auto reports = metrics.get_counter_reports();
  ASSERT_EQ(reports.size(), 1);
  EXPECT_EQ(reports[0].total_count, 2);
}

TEST_F(MetricsServiceTest, PercentileAccuracy) {
  MetricsService<MockClock> metrics;
  
  // Record a known distribution: 1, 2, 3, ..., 100 microseconds
  for (int i = 1; i <= 100; ++i) {
    metrics.record_latency("percentile_test", std::chrono::microseconds(i));
  }
  
  auto reports = metrics.get_latency_reports();
  ASSERT_EQ(reports.size(), 1);
  
  const auto& report = reports[0];
  EXPECT_EQ(report.sample_count, 100);
  
  // P50 should be around 50-51µs
  EXPECT_GT(report.p50_microseconds, 45);
  EXPECT_LT(report.p50_microseconds, 55);
  
  // P90 should be around 90-91µs
  EXPECT_GT(report.p90_microseconds, 85);
  EXPECT_LT(report.p90_microseconds, 95);
  
  // P99 should be around 99µs
  EXPECT_GT(report.p99_microseconds, 95);
  EXPECT_LE(report.p99_microseconds, 100);
}

}  // namespace futility::metrics