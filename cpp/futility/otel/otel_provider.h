#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "opentelemetry/metrics/provider.h"

namespace futility::otel {

struct OtelConfig {
  std::string service_name = "moonbase-service";
  std::string service_version = "1.0.0";
  std::string otlp_endpoint = "http://localhost:4318/v1/metrics";
  std::chrono::seconds export_interval{10};
  bool enable_metrics = true;
};

class OtelProvider {
 public:
  static void Initialize(const OtelConfig& config);
  static std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> GetMeterProvider();
  static void Shutdown();

 private:
  static std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> meter_provider_;
  static bool initialized_;
};

}  // namespace futility::otel