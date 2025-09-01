#ifndef CPP_FUTILITY_METRICS_TIME_BUCKET_H
#define CPP_FUTILITY_METRICS_TIME_BUCKET_H

#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <vector>

namespace futility::metrics {

template<typename T>
struct TimeBucket {
  std::chrono::steady_clock::time_point timestamp;
  std::vector<T> values;
  std::atomic<size_t> count{0};
  mutable std::shared_mutex mutex;
  
  explicit TimeBucket(std::chrono::steady_clock::time_point ts = std::chrono::steady_clock::now()) 
      : timestamp(ts) {
    values.reserve(64); // Reserve space for initial values
  }
  
  void add_value(const T& value) {
    std::unique_lock lock(mutex);
    values.push_back(value);
    count.fetch_add(1, std::memory_order_relaxed);
  }
  
  void reset() {
    std::unique_lock lock(mutex);
    values.clear();
    count.store(0, std::memory_order_relaxed);
  }
  
  bool is_expired(std::chrono::steady_clock::time_point now, 
                  std::chrono::duration<int64_t> retention) const {
    return now - timestamp > retention;
  }
  
  std::vector<T> get_values() const {
    std::shared_lock lock(mutex);
    return values; // Copy the vector
  }
  
  size_t get_count() const {
    return count.load(std::memory_order_relaxed);
  }
  
  size_t estimated_memory_usage() const {
    std::shared_lock lock(mutex);
    return sizeof(*this) + values.capacity() * sizeof(T);
  }
};

}  // namespace futility::metrics

#endif