#include "cpp/futility/metrics/latency_histogram.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace futility::metrics {

class LatencyHistogramTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(LatencyHistogramTest, BasicPercentileCalculation) {
  LatencyHistogram histogram;
  
  // Record latencies: 100µs, 200µs, 300µs, ..., 1000µs
  for (int i = 1; i <= 10; ++i) {
    histogram.record(i * 100);
  }
  
  EXPECT_EQ(histogram.get_total_count(), 10);
  
  // Check percentiles (values will be approximate due to bucketing)
  double p50 = histogram.get_percentile(50.0);
  double p90 = histogram.get_percentile(90.0);
  double p99 = histogram.get_percentile(99.0);
  
  EXPECT_GT(p50, 400);  // Should be around 550µs
  EXPECT_LT(p50, 700);
  
  EXPECT_GT(p90, 800);  // Should be around 950µs
  EXPECT_LT(p90, 1100);
  
  EXPECT_GT(p99, 900);  // Should be close to 1000µs
  EXPECT_LE(p99, 1000);
}

TEST_F(LatencyHistogramTest, StatisticalFunctions) {
  LatencyHistogram histogram;
  
  std::vector<int64_t> values = {100, 200, 300, 400, 500};
  for (auto value : values) {
    histogram.record(value);
  }
  
  EXPECT_EQ(histogram.get_total_count(), 5);
  EXPECT_EQ(histogram.get_min(), 100);
  EXPECT_EQ(histogram.get_max(), 500);
  
  double mean = histogram.get_mean();
  EXPECT_GT(mean, 250);
  EXPECT_LT(mean, 350);
}

TEST_F(LatencyHistogramTest, RecordMultiple) {
  LatencyHistogram histogram;
  
  histogram.record_multiple(100, 5);  // Record value 100 five times
  histogram.record_multiple(200, 3);  // Record value 200 three times
  
  EXPECT_EQ(histogram.get_total_count(), 8);
  EXPECT_EQ(histogram.get_min(), 100);
  EXPECT_EQ(histogram.get_max(), 200);
}

TEST_F(LatencyHistogramTest, ValueRangeHandling) {
  // Test with custom range
  LatencyHistogram histogram(10, 10000, 2);  // 10µs to 10ms, 2 significant figures
  
  histogram.record(50);
  histogram.record(500);
  histogram.record(5000);
  
  EXPECT_EQ(histogram.get_total_count(), 3);
  EXPECT_GE(histogram.get_min(), 10);  // May be adjusted due to bucketing
  EXPECT_LE(histogram.get_max(), 10000);
}

TEST_F(LatencyHistogramTest, Reset) {
  LatencyHistogram histogram;
  
  for (int i = 1; i <= 100; ++i) {
    histogram.record(i * 10);
  }
  
  EXPECT_EQ(histogram.get_total_count(), 100);
  EXPECT_GT(histogram.get_mean(), 0);
  
  histogram.reset();
  
  EXPECT_EQ(histogram.get_total_count(), 0);
  EXPECT_EQ(histogram.get_mean(), 0);
  EXPECT_EQ(histogram.get_percentile(50.0), 0);
}

TEST_F(LatencyHistogramTest, ThreadSafety) {
  LatencyHistogram histogram;
  const int num_threads = 10;
  const int records_per_thread = 100;
  
  std::vector<std::thread> threads;
  
  // Spawn threads that record values concurrently
  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&histogram, i]() {
      for (int j = 0; j < records_per_thread; ++j) {
        histogram.record((i + 1) * 100 + j);
      }
    });
  }
  
  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }
  
  // Verify total count
  EXPECT_EQ(histogram.get_total_count(), num_threads * records_per_thread);
  
  // Verify we can read percentiles without crashing
  double p50 = histogram.get_percentile(50.0);
  double p90 = histogram.get_percentile(90.0);
  double p99 = histogram.get_percentile(99.0);
  
  EXPECT_GT(p50, 0);
  EXPECT_GT(p90, p50);
  EXPECT_GT(p99, p90);
}

TEST_F(LatencyHistogramTest, MoveSemantics) {
  LatencyHistogram histogram1;
  
  histogram1.record(100);
  histogram1.record(200);
  EXPECT_EQ(histogram1.get_total_count(), 2);
  
  // Move construct
  LatencyHistogram histogram2 = std::move(histogram1);
  EXPECT_EQ(histogram2.get_total_count(), 2);
  EXPECT_EQ(histogram2.get_min(), 100);
  EXPECT_EQ(histogram2.get_max(), 200);
  
  // Move assign
  LatencyHistogram histogram3;
  histogram3 = std::move(histogram2);
  EXPECT_EQ(histogram3.get_total_count(), 2);
  EXPECT_EQ(histogram3.get_min(), 100);
  EXPECT_EQ(histogram3.get_max(), 200);
}

TEST_F(LatencyHistogramTest, InvalidParameters) {
  // Test invalid constructor parameters
  EXPECT_THROW(LatencyHistogram(-1, 1000), std::invalid_argument);  // negative min
  EXPECT_THROW(LatencyHistogram(1000, 100), std::invalid_argument);  // max < min
  EXPECT_THROW(LatencyHistogram(1, 1000, 0), std::invalid_argument);  // invalid sig figs
  EXPECT_THROW(LatencyHistogram(1, 1000, 6), std::invalid_argument);  // invalid sig figs
}

TEST_F(LatencyHistogramTest, EmptyHistogram) {
  LatencyHistogram histogram;
  
  // Empty histogram should return 0 for all queries
  EXPECT_EQ(histogram.get_total_count(), 0);
  EXPECT_EQ(histogram.get_min(), 0);
  EXPECT_EQ(histogram.get_max(), 0);
  EXPECT_EQ(histogram.get_mean(), 0);
  EXPECT_EQ(histogram.get_percentile(50.0), 0);
}

TEST_F(LatencyHistogramTest, LargeValues) {
  // Test with values near the upper limit (1 hour = 3,600,000,000 µs)
  LatencyHistogram histogram;
  
  histogram.record(3600000000 - 1000);  // Just under 1 hour
  histogram.record(1000000);            // 1 second
  histogram.record(100000);             // 100ms
  
  EXPECT_EQ(histogram.get_total_count(), 3);
  // HdrHistogram may quantize values, so allow some tolerance
  EXPECT_GE(histogram.get_min(), 95000);  // Allow some quantization error
  EXPECT_LE(histogram.get_min(), 105000);
  EXPECT_GE(histogram.get_max(), 3599000000);  // Allow some quantization error
  EXPECT_LE(histogram.get_max(), 3601000000);
}

}  // namespace futility::metrics