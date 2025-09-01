#ifndef CPP_FUTILITY_METRICS_LATENCY_HISTOGRAM_H
#define CPP_FUTILITY_METRICS_LATENCY_HISTOGRAM_H

#include <mutex>

extern "C" {
#include "src/hdr_histogram.h"
}

namespace futility::metrics {

class LatencyHistogram {
 public:
  explicit LatencyHistogram(int64_t min_value = 1,           // 1 microsecond
                           int64_t max_value = 3600000000,   // 1 hour in microseconds  
                           int significant_figures = 3);
  ~LatencyHistogram();
  
  // Non-copyable due to C struct ownership
  LatencyHistogram(const LatencyHistogram&) = delete;
  LatencyHistogram& operator=(const LatencyHistogram&) = delete;
  
  // Movable
  LatencyHistogram(LatencyHistogram&& other) noexcept;
  LatencyHistogram& operator=(LatencyHistogram&& other) noexcept;
  
  void record(int64_t value_microseconds);
  void record_multiple(int64_t value_microseconds, int64_t count);
  
  // Thread-safe percentile queries (using internal mutex)
  double get_percentile(double percentile) const;  // 0.0-100.0
  int64_t get_value_at_percentile(double percentile) const;
  
  // Statistical functions
  int64_t get_min() const;
  int64_t get_max() const;
  double get_mean() const;
  int64_t get_total_count() const;
  
  void reset();
  
 private:
  struct hdr_histogram* histogram_;
  mutable std::mutex histogram_mutex_;  // Protect non-atomic operations
  
  void ensure_initialized();
};

}  // namespace futility::metrics

#endif