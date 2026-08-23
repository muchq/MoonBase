#pragma once

/// @file otel_provider.h
/// @brief OpenTelemetry metrics provider initialization and configuration.
///
/// This module provides a simple interface for setting up OpenTelemetry metrics
/// export via OTLP (OpenTelemetry Protocol). It handles the creation and
/// configuration of the meter provider, metric reader, and OTLP exporter.
///
/// Example usage:
/// @code
///   // Initialize OpenTelemetry at application startup
///   futility::otel::OtelConfig config{
///     .service_name = "my-service",
///     .service_version = "1.0.0",
///     .otlp_endpoint = "http://otel-collector:4318/v1/metrics",
///     .export_interval = std::chrono::seconds(30),
///     .enable_metrics = true
///   };
///   futility::otel::OtelProvider provider(config);
///
///   // The provider sets the global meter provider automatically.
///   // Use MetricsRecorder for recording metrics.
/// @endcode
///
/// Environment Variables:
/// - OTEL_EXPORTER_OTLP_ENDPOINT: Overrides config.otlp_endpoint if set.

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

namespace futility::otel {

/// @brief Explicit bucket boundaries, in microseconds, for latency histograms.
///
/// Runs from 100µs to 10s. Registered as an SDK view by OtelProvider, because
/// opentelemetry-cpp offers no way to pass bounds to CreateUInt64Histogram —
/// without a view every histogram gets the SDK defaults.
///
/// Those defaults (0, 5, 10, ... 10000) are shaped for milliseconds. Every
/// MetricsRecorder::RecordLatency observation is microseconds, so the top
/// finite bucket meant 10ms: anything slower landed in +Inf, and
/// histogram_quantile answers the highest finite bound when the rank lands
/// there, so p95 read a flat 10000 no matter how slow the service got (#1286).
/// Confirmed in production on portrait, which was serving at a real p95 near
/// one second while its tile held steady at 10ms (#1287).
///
/// yodel (Java, HttpServerMetrics.BUCKET_BOUNDS) and server_pal (Rust,
/// HTTP_LATENCY_BUCKET_BOUNDS_MICROS) declare this same set. prom_proxy runs
/// one histogram_quantile expression across all three languages, which only
/// compares like with like if the layouts match — so the three move together
/// or not at all. //domains/platform/libs/otel_contract pins them equal.
inline constexpr std::array<double, 15> kHttpLatencyBucketBoundsMicros = {
    100,   250,    500,    1000,   2500,    5000,    10000,   25000,
    50000, 100000, 250000, 500000, 1000000, 2500000, 10000000};

/// @brief Registers the explicit-bucket view for latency histograms.
///
/// Called by OtelProvider's constructor; exposed so a test can drive it
/// against an in-memory reader. Without a view, opentelemetry-cpp gives every
/// histogram the SDK's millisecond-shaped defaults, which is #1286 — and the
/// bucket constant alone cannot pin that, since a constant nothing applies is
/// exactly the bug.
///
/// Matches any histogram whose name ends in `_microseconds`, which is the
/// suffix MetricsRecorder::RecordLatency appends and nothing else produces.
/// RecordDistribution keeps its bare name and is deliberately left on the
/// defaults.
void RegisterLatencyBucketView(opentelemetry::sdk::metrics::MeterProvider& meter_provider);

/// @brief Registers explicit bucket boundaries for one named histogram.
///
/// The only way to give an instrument its own bounds: opentelemetry-cpp
/// offers no way to pass them to CreateUInt64Histogram, so without a view
/// every histogram takes the SDK's millisecond-shaped defaults (0 … 10000)
/// — #1286 again, one instrument at a time.
///
/// `metric_name` is matched exactly. Use it for a distribution whose range
/// is nothing like an HTTP request's: an index run takes minutes, so on the
/// defaults every observation lands in +Inf and histogram_quantile answers
/// the highest finite bound forever. yodel's CustomMetrics.defineDistribution
/// is the Java side of the same job, and a series both languages write needs
/// the same layout in each or a quantile across them compares nothing.
void RegisterHistogramBounds(opentelemetry::sdk::metrics::MeterProvider& meter_provider,
                             const std::string& metric_name, const std::vector<double>& bounds);

/// @brief Configuration for the OpenTelemetry provider.
struct OtelConfig {
  /// Service name reported in metrics.
  std::string service_name = "moonbase-service";

  /// Service version reported in metrics.
  std::string service_version = "1.0.0";

  /// OTLP HTTP endpoint for exporting metrics.
  /// Can be overridden by OTEL_EXPORTER_OTLP_ENDPOINT environment variable.
  std::string otlp_endpoint = "http://localhost:4318/v1/metrics";

  /// Interval between metric exports.
  std::chrono::seconds export_interval{10};

  /// Explicit bucket boundaries per histogram name, applied as SDK views.
  /// See RegisterHistogramBounds.
  std::map<std::string, std::vector<double>> histogram_bounds = {};

  /// Whether metrics collection is enabled. Set to false to disable all metrics.
  bool enable_metrics = true;
};

/// @brief Manages OpenTelemetry metrics provider lifecycle.
///
/// Creates and configures an OTLP HTTP metric exporter and sets it as the
/// global meter provider. The provider should be created once at application
/// startup and kept alive for the duration of the application.
///
/// When destroyed, the provider resets the global meter provider.
class OtelProvider {
 public:
  /// @brief Initializes OpenTelemetry metrics with the given configuration.
  /// @param config The OpenTelemetry configuration.
  explicit OtelProvider(const OtelConfig& config);

  /// @brief Cleans up and resets the global meter provider.
  ~OtelProvider();

  // Non-copyable, non-movable (manages global state)
  OtelProvider(const OtelProvider&) = delete;
  OtelProvider& operator=(const OtelProvider&) = delete;
  OtelProvider(OtelProvider&&) = delete;
  OtelProvider& operator=(OtelProvider&&) = delete;

  /// @brief Returns the meter provider for creating custom meters.
  /// @return The meter provider, or nullptr if metrics are disabled.
  std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> GetMeterProvider() const;

 private:
  std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> meter_provider_;
  bool metrics_enabled_;
};

}  // namespace futility::otel