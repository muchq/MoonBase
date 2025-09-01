#ifndef CPP_FUTILITY_METRICS_METRICS_SERVICE_H
#define CPP_FUTILITY_METRICS_METRICS_SERVICE_H

#include <algorithm>
#include <chrono>
#include <memory>
#include <numeric>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpp/futility/metrics/sliding_window.h"
#include "cpp/futility/metrics/latency_histogram.h"

namespace futility::metrics {

enum class EventType {
  LATENCY,      // Timing measurements (microseconds)
  COUNTER,      // Simple counts/rates
  GAUGE,        // Current value snapshots
  CACHE_HIT,    // Cache hit/miss tracking
};

template<typename Clock = std::chrono::steady_clock>
class MetricsService {
 public:
  explicit MetricsService(const MetricsConfig& config = {});
  ~MetricsService() = default;

  // Recording API
  void record_latency(const std::string& metric_name, 
                     std::chrono::microseconds duration);
  void record_counter(const std::string& metric_name, int64_t value = 1);
  void record_gauge(const std::string& metric_name, double value);
  void record_cache_event(const std::string& metric_name, bool hit);

  // Reporting API
  struct LatencyReport {
    std::string metric_name;
    size_t sample_count;
    double p50_microseconds;
    double p90_microseconds;
    double p95_microseconds;
    double p99_microseconds;
    double mean_microseconds;
    typename Clock::time_point window_start;
    typename Clock::time_point window_end;
  };

  struct CounterReport {
    std::string metric_name;
    int64_t total_count;
    double rate_per_second;
    typename Clock::time_point window_start;
    typename Clock::time_point window_end;
  };

  struct CacheReport {
    std::string metric_name;
    size_t total_requests;
    size_t hit_count;
    double hit_rate;
    typename Clock::time_point window_start;
    typename Clock::time_point window_end;
  };

  std::vector<LatencyReport> get_latency_reports() const;
  std::vector<CounterReport> get_counter_reports() const;
  std::vector<CacheReport> get_cache_reports() const;
  
  // Utility
  void cleanup_expired_data();
  size_t get_metric_count() const;

 private:
  struct LatencyMetric {
    SlidingWindow<std::chrono::microseconds, Clock> window;
    std::unique_ptr<LatencyHistogram> histogram;
    std::string name;
    
    explicit LatencyMetric(const std::string& metric_name, const MetricsConfig& config) 
        : window(config), histogram(std::make_unique<LatencyHistogram>()), name(metric_name) {}
  };

  struct CounterMetric {
    SlidingWindow<int64_t, Clock> window;
    std::string name;
    
    explicit CounterMetric(const std::string& metric_name, const MetricsConfig& config)
        : window(config), name(metric_name) {}
  };

  struct CacheMetric {
    SlidingWindow<bool, Clock> window;  // true = hit, false = miss
    std::string name;
    
    explicit CacheMetric(const std::string& metric_name, const MetricsConfig& config)
        : window(config), name(metric_name) {}
  };

  MetricsConfig config_;
  
  mutable std::shared_mutex metrics_mutex_;
  std::unordered_map<std::string, std::unique_ptr<LatencyMetric>> latency_metrics_;
  std::unordered_map<std::string, std::unique_ptr<CounterMetric>> counter_metrics_;
  std::unordered_map<std::string, std::unique_ptr<CacheMetric>> cache_metrics_;
  
  typename Clock::time_point last_cleanup_;
  std::chrono::duration<int64_t> cleanup_interval_ = std::chrono::minutes(5);

  // Thread safety helpers
  template<typename MetricMap>
  auto get_or_create_metric(MetricMap& map, const std::string& name) -> typename MetricMap::mapped_type::element_type*;
  
  void maybe_cleanup();
};

// Template implementation
template<typename Clock>
MetricsService<Clock>::MetricsService(const MetricsConfig& config)
    : config_(config), last_cleanup_(Clock::now()) {}

template<typename Clock>
void MetricsService<Clock>::record_latency(const std::string& metric_name, 
                                          std::chrono::microseconds duration) {
  maybe_cleanup();
  
  std::unique_lock lock(metrics_mutex_);
  auto* metric = get_or_create_metric(latency_metrics_, metric_name);
  if (metric) {
    metric->window.record(duration);
    metric->histogram->record(duration.count());
  }
}

template<typename Clock>
void MetricsService<Clock>::record_counter(const std::string& metric_name, int64_t value) {
  maybe_cleanup();
  
  std::unique_lock lock(metrics_mutex_);
  auto* metric = get_or_create_metric(counter_metrics_, metric_name);
  if (metric) {
    metric->window.record(value);
  }
}

template<typename Clock>
void MetricsService<Clock>::record_gauge(const std::string& metric_name, double value) {
  // For now, treat gauges as counters - we'll enhance this later
  record_counter(metric_name, static_cast<int64_t>(value));
}

template<typename Clock>
void MetricsService<Clock>::record_cache_event(const std::string& metric_name, bool hit) {
  maybe_cleanup();
  
  std::unique_lock lock(metrics_mutex_);
  auto* metric = get_or_create_metric(cache_metrics_, metric_name);
  if (metric) {
    metric->window.record(hit);
  }
}

template<typename Clock>
std::vector<typename MetricsService<Clock>::LatencyReport> 
MetricsService<Clock>::get_latency_reports() const {
  std::shared_lock lock(metrics_mutex_);
  std::vector<LatencyReport> reports;
  
  auto now = Clock::now();
  auto window_start = now - config_.retention_period;
  
  for (const auto& [name, metric] : latency_metrics_) {
    if (metric->histogram->get_total_count() > 0) {
      LatencyReport report;
      report.metric_name = name;
      report.sample_count = metric->histogram->get_total_count();
      report.p50_microseconds = metric->histogram->get_percentile(50.0);
      report.p90_microseconds = metric->histogram->get_percentile(90.0);
      report.p95_microseconds = metric->histogram->get_percentile(95.0);
      report.p99_microseconds = metric->histogram->get_percentile(99.0);
      report.mean_microseconds = metric->histogram->get_mean();
      report.window_start = window_start;
      report.window_end = now;
      reports.push_back(report);
    }
  }
  
  return reports;
}

template<typename Clock>
std::vector<typename MetricsService<Clock>::CounterReport> 
MetricsService<Clock>::get_counter_reports() const {
  std::shared_lock lock(metrics_mutex_);
  std::vector<CounterReport> reports;
  
  auto now = Clock::now();
  auto window_start = now - config_.retention_period;
  auto window_duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(
      config_.retention_period).count();
  
  for (const auto& [name, metric] : counter_metrics_) {
    auto values = metric->window.get_values_in_window();
    if (!values.empty()) {
      CounterReport report;
      report.metric_name = name;
      report.total_count = std::accumulate(values.begin(), values.end(), 0LL);
      report.rate_per_second = window_duration_seconds > 0 ? 
          static_cast<double>(report.total_count) / window_duration_seconds : 0.0;
      report.window_start = window_start;
      report.window_end = now;
      reports.push_back(report);
    }
  }
  
  return reports;
}

template<typename Clock>
std::vector<typename MetricsService<Clock>::CacheReport> 
MetricsService<Clock>::get_cache_reports() const {
  std::shared_lock lock(metrics_mutex_);
  std::vector<CacheReport> reports;
  
  auto now = Clock::now();
  auto window_start = now - config_.retention_period;
  
  for (const auto& [name, metric] : cache_metrics_) {
    auto values = metric->window.get_values_in_window();
    if (!values.empty()) {
      CacheReport report;
      report.metric_name = name;
      report.total_requests = values.size();
      report.hit_count = std::count(values.begin(), values.end(), true);
      report.hit_rate = report.total_requests > 0 ? 
          static_cast<double>(report.hit_count) / report.total_requests : 0.0;
      report.window_start = window_start;
      report.window_end = now;
      reports.push_back(report);
    }
  }
  
  return reports;
}

template<typename Clock>
void MetricsService<Clock>::cleanup_expired_data() {
  std::unique_lock lock(metrics_mutex_);
  
  for (auto& [name, metric] : latency_metrics_) {
    metric->window.cleanup_expired_buckets();
  }
  
  for (auto& [name, metric] : counter_metrics_) {
    metric->window.cleanup_expired_buckets();
  }
  
  for (auto& [name, metric] : cache_metrics_) {
    metric->window.cleanup_expired_buckets();
  }
  
  last_cleanup_ = Clock::now();
}

template<typename Clock>
size_t MetricsService<Clock>::get_metric_count() const {
  std::shared_lock lock(metrics_mutex_);
  return latency_metrics_.size() + counter_metrics_.size() + cache_metrics_.size();
}

template<typename Clock>
template<typename MetricMap>
auto MetricsService<Clock>::get_or_create_metric(MetricMap& map, const std::string& name) 
    -> typename MetricMap::mapped_type::element_type* {
  
  auto it = map.find(name);
  if (it != map.end()) {
    return it->second.get();
  }
  
  // Check max metrics limit
  if (latency_metrics_.size() + counter_metrics_.size() + cache_metrics_.size() >= config_.max_metrics) {
    return nullptr;
  }
  
  // Create new metric
  using ValueType = typename MetricMap::mapped_type::element_type;
  auto metric = std::make_unique<ValueType>(name, config_);
  auto* result = metric.get();
  map[name] = std::move(metric);
  
  return result;
}

template<typename Clock>
void MetricsService<Clock>::maybe_cleanup() {
  auto now = Clock::now();
  if (now - last_cleanup_ >= cleanup_interval_) {
    cleanup_expired_data();
  }
}

}  // namespace futility::metrics

#endif