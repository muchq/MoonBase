#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <map>
#include <unordered_map>

#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace futility::otel {

class MetricsRecorder {
 public:
  explicit MetricsRecorder(const std::string& service_name);
  ~MetricsRecorder() = default;

  // Counter for event counting
  void RecordCounter(const std::string& metric_name, 
                     int64_t value = 1,
                     const std::map<std::string, std::string>& attributes = {});
  
  // Histogram for latency measurements
  void RecordLatency(const std::string& metric_name, 
                     std::chrono::microseconds duration,
                     const std::map<std::string, std::string>& attributes = {});
  
  // Gauge for current values (implemented as UpDownCounter)
  void RecordGauge(const std::string& metric_name,
                   double value,
                   const std::map<std::string, std::string>& attributes = {});

 private:
  std::shared_ptr<opentelemetry::metrics::Meter> meter_;
  
  // Cache metric instruments to avoid recreating them
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>> counters_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>> histograms_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>> gauges_;
};

} // namespace futility::otel