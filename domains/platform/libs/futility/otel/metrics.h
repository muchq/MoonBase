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
#include <mutex>
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
  virtual ~MetricsRecorder() = default;

  /// @brief The service this recorder was built for.
  ///
  /// Metric families that are labeled by service — the standard http_server_*
  /// and cache_* blocks — read it from here rather than being handed the name
  /// a second time, so a series' label cannot disagree with the meter it was
  /// recorded through.
  const std::string& service_name() const { return service_name_; }

  // The record methods are virtual for exactly one reason: tests wrap a
  // recorder that captures what a service counts — names, values, and
  // labels — and assert on it (including what must never appear in a
  // label). Production code always uses this class directly.

  /// @brief Records a counter metric (monotonically increasing).
  ///
  /// Use for counting events: requests, errors, messages processed, etc.
  ///
  /// @param metric_name The metric name (e.g., "http_requests_total").
  /// @param value The amount to increment (default: 1).
  /// @param attributes Optional key-value labels for the metric.
  virtual void RecordCounter(const std::string& metric_name, int64_t value = 1,
                             const std::map<std::string, std::string>& attributes = {});

  /// @brief Declares a counter series ahead of its first event, so it exports
  ///        as 0 from process start.
  ///
  /// Without this a counter springs into existence already carrying the value
  /// of the first event it counted: the instrument is created lazily on the
  /// first RecordCounter, so the series' very first exported sample is that
  /// event's value. increase() and rate() measure the change *between*
  /// samples, so with nothing earlier to measure from that first event shows
  /// no increase at all — forever. The counter is not wrong; every dashboard
  /// built on it reads zero, which is worse than a missing tile because it
  /// looks like an answer.
  ///
  /// That is what every process does after every deploy, so the first event of
  /// anything is uncounted until a second one follows it. Diagnosed on the
  /// Java rail, where an overnight run of a thousand games left a panel of
  /// zeros (https://github.com/muchq/MoonBase/issues/1323); this rail creates
  /// its instruments the same way.
  ///
  /// Declare the label sets whose values are known up front — outcomes,
  /// error kinds, states. A label carrying client input has no series to
  /// declare (and is a cardinality problem besides); map it to a bounded
  /// kind and declare that instead. A label whose values are a generated
  /// union's case names *is* declarable — the set is the schema's — as long
  /// as a test pins the declared roster to the schema in both directions
  /// (golf_hub's StreamSeriesMatchTheModelUnions is the worked example).
  ///
  /// Idempotent, and harmless once events have arrived: adding zero to a
  /// counter leaves its total alone.
  ///
  /// @param metric_name The metric name (e.g., "trace_requests_failed").
  /// @param attributes The label set to declare (default: none).
  virtual void DeclareCounter(const std::string& metric_name,
                              const std::map<std::string, std::string>& attributes = {});

  /// @brief Records a histogram metric for latency/duration measurements.
  ///
  /// Use for timing operations: request latency, processing time, etc.
  /// Values are recorded in microseconds.
  ///
  /// @param metric_name The metric name (e.g., "request_duration_us").
  /// @param duration The duration to record.
  /// @param attributes Optional key-value labels for the metric.
  virtual void RecordLatency(const std::string& metric_name, std::chrono::microseconds duration,
                             const std::map<std::string, std::string>& attributes = {});

  /// @brief Records one observation of a per-event quantity.
  ///
  /// Use for distributions of request-scoped values (payload sizes, scene
  /// complexity). The histogram keeps the name as given; query the
  /// windowed average as rate(<name>_sum)/rate(<name>_count). Not for
  /// point-in-time levels — that is RecordGauge's delta form.
  ///
  /// A histogram cannot be declared at zero — recording 0 is an observation
  /// that biases exactly that average — so its series carry a
  /// first-observation gap until real data arrives (#1384). If the mean is
  /// the only consumer, prefer two counters (the sum and the event count),
  /// which DeclareCounter can baseline: golf_hub's chat_catch_up_drains
  /// replaced a distribution this way. Reach for a histogram when buckets
  /// or percentiles are actually read.
  ///
  /// @param metric_name The metric name (e.g., "scene_sphere_count").
  /// @param value The observed value.
  /// @param attributes Optional key-value labels for the metric.
  virtual void RecordDistribution(const std::string& metric_name, double value,
                                  const std::map<std::string, std::string>& attributes = {});

  /// @brief Records a gauge metric (point-in-time value).
  ///
  /// Use for current state: active connections, queue depth, memory usage, etc.
  /// Implemented as an UpDownCounter.
  ///
  /// @param metric_name The metric name (e.g., "connections_active").
  /// @param value The current value to record.
  /// @param attributes Optional key-value labels for the metric.
  virtual void RecordGauge(const std::string& metric_name, double value,
                           const std::map<std::string, std::string>& attributes = {});

 private:
  /// The cached instrument, created under mu_ on first use. Element
  /// pointers survive rehash and instruments record thread-safely, so
  /// callers record outside the lock.
  template <typename Instrument, typename Factory>
  Instrument* FindOrCreate(std::unordered_map<std::string, std::unique_ptr<Instrument>>& cache,
                           const std::string& metric_name, const Factory& make);

  const std::string service_name_;
  std::shared_ptr<opentelemetry::metrics::Meter> meter_;

  // Cache metric instruments to avoid recreating them. Recorders are
  // called from arbitrary threads; mu_ guards the caches.
  std::mutex mu_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>>
      counters_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>>
      histograms_;
  std::unordered_map<std::string, std::unique_ptr<opentelemetry::metrics::UpDownCounter<int64_t>>>
      gauges_;
};

}  // namespace futility::otel