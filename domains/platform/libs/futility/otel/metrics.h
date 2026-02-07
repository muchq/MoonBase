#pragma once

/// @file metrics.h
/// @brief High-level API for recording OpenTelemetry metrics.
///
/// Provides a simplified interface for recording common metric types
/// (counters, histograms, gauges) without dealing with OpenTelemetry
/// instrument creation details.
///
/// Example usage:
/// @code
///   // Create a recorder (typically one per service)
///   MetricsRecorder metrics("my-service");
///
///   // Count events
///   metrics.RecordCounter("requests_total", 1, {{"method", "GET"}, {"path", "/api"}});
///
///   // Record latencies
///   auto start = std::chrono::steady_clock::now();
///   // ... do work ...
///   auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
///       std::chrono::steady_clock::now() - start);
///   metrics.RecordLatency("request_duration_us", duration, {{"method", "GET"}});
///
///   // Track current values
///   metrics.RecordGauge("connections_active", active_connections);
/// @endcode
///
/// @note Requires OtelProvider to be initialized first for metrics to be exported.

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace futility::otel {

/// @brief High-level API for recording OpenTelemetry metrics.
///
/// Simplifies metric recording by handling instrument creation and caching.
/// Supports three metric types:
/// - Counters: For monotonically increasing values (requests, errors, etc.)
/// - Histograms: For distributions (latencies, sizes, etc.)
/// - Gauges: For point-in-time values (connections, queue depth, etc.)
///
/// Instruments are created lazily and cached for reuse.
class MetricsRecorder {
 public:
  /// @brief Creates a metrics recorder for the given service.
  /// @param service_name The service name used to create the meter.
  explicit MetricsRecorder(const std::string& service_name);
  ~MetricsRecorder() = default;

  /// @brief Records a counter metric (monotonically increasing).
  ///
  /// Use for counting events: requests, errors, messages processed, etc.
  ///
  /// @param metric_name The metric name (e.g., "http_requests_total").
  /// @param value The amount to increment (default: 1).
  /// @param attributes Optional key-value labels for the metric.
  void RecordCounter(const std::string& metric_name, int64_t value = 1,
                     const std::map<std::string, std::string>& attributes = {});

  /// @brief Records a histogram metric for latency/duration measurements.
  ///
  /// Use for timing operations: request latency, processing time, etc.
  /// Values are recorded in microseconds.
  ///
  /// @param metric_name The metric name (e.g., "request_duration_us").
  /// @param duration The duration to record.
  /// @param attributes Optional key-value labels for the metric.
  void RecordLatency(const std::string& metric_name, std::chrono::microseconds duration,
                     const std::map<std::string, std::string>& attributes = {});

  /// @brief Records a gauge metric (point-in-time value).
  ///
  /// Use for current state: active connections, queue depth, memory usage, etc.
  /// Implemented as an UpDownCounter.
  ///
  /// @param metric_name The metric name (e.g., "connections_active").
  /// @param value The current value to record.
  /// @param attributes Optional key-value labels for the metric.
  void RecordGauge(const std::string& metric_name, double value,
                   const std::map<std::string, std::string>& attributes = {});

 private:
  std::shared_ptr<opentelemetry::metrics::Meter> meter_;

  // Cache metric instruments to avoid recreating them
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>>
      counters_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>>
      histograms_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>>
      gauges_;
};

}  // namespace futility::otel