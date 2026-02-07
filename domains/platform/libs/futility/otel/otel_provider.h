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

#include <chrono>
#include <memory>
#include <string>

#include "opentelemetry/metrics/provider.h"

namespace futility::otel {

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