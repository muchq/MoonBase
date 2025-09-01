#ifndef CPP_FUTILITY_METRICS_SLIDING_WINDOW_H
#define CPP_FUTILITY_METRICS_SLIDING_WINDOW_H

#include <chrono>
#include <deque>
#include <shared_mutex>
#include <vector>

#include "cpp/futility/metrics/time_bucket.h"

namespace futility::metrics {

struct MetricsConfig {
  std::chrono::duration<int64_t> retention_period = std::chrono::hours(24 * 7);  // 7 days
  std::chrono::duration<int64_t> bucket_size = std::chrono::minutes(1);          // 1 minute buckets
  size_t max_metrics = 10000;                                                    // Limit total metrics
  size_t histogram_buckets = 1000;                                              // For percentile calculation
  bool enable_system_metrics = true;                                            // Auto-collect system metrics
  std::chrono::duration<int64_t> system_metrics_interval = std::chrono::seconds(30); // System collection frequency
};

template<typename T, typename Clock = std::chrono::steady_clock>
class SlidingWindow {
 public:
  explicit SlidingWindow(const MetricsConfig& config = {})
      : config_(config), last_cleanup_(Clock::now()) {}
  
  void record(const T& value) {
    std::unique_lock lock(mutex_);
    
    // Evict expired buckets before adding new data
    evict_expired_buckets_unsafe();
    
    auto& current_bucket = get_or_create_current_bucket();
    current_bucket.add_value(value);
  }
  
  std::vector<T> get_values_in_window() const {
    std::shared_lock lock(mutex_);
    std::vector<T> result;
    
    for (const auto& bucket : buckets_) {
      if (!bucket.is_expired(Clock::now(), config_.retention_period)) {
        auto bucket_values = bucket.get_values();
        result.insert(result.end(), bucket_values.begin(), bucket_values.end());
      }
    }
    
    return result;
  }
  
  void cleanup_expired_buckets() {
    std::unique_lock lock(mutex_);
    evict_expired_buckets_unsafe();
  }
  
  size_t bucket_count() const {
    std::shared_lock lock(mutex_);
    return buckets_.size();
  }
  
  size_t estimated_memory_usage() const {
    std::shared_lock lock(mutex_);
    size_t total = sizeof(*this);
    for (const auto& bucket : buckets_) {
      total += bucket.estimated_memory_usage();
    }
    return total;
  }
  
  size_t total_sample_count() const {
    std::shared_lock lock(mutex_);
    size_t total = 0;
    for (const auto& bucket : buckets_) {
      total += bucket.get_count();
    }
    return total;
  }
  
 private:
  std::deque<TimeBucket<T>> buckets_;
  MetricsConfig config_;
  mutable std::shared_mutex mutex_;
  typename Clock::time_point last_cleanup_;
  
  void evict_expired_buckets_unsafe() {
    auto now = Clock::now();
    auto cutoff_time = now - config_.retention_period;
    
    // Remove buckets older than retention period
    while (!buckets_.empty() && buckets_.front().timestamp < cutoff_time) {
      buckets_.pop_front();  // Automatic memory deallocation
    }
    
    last_cleanup_ = now;
  }
  
  TimeBucket<T>& get_or_create_current_bucket() {
    auto now = Clock::now();
    
    // Check if we need a new bucket (or if we have no buckets)
    if (buckets_.empty() || 
        now - buckets_.back().timestamp > config_.bucket_size) {
      buckets_.emplace_back(now);
    }
    
    return buckets_.back();
  }
};

}  // namespace futility::metrics

#endif