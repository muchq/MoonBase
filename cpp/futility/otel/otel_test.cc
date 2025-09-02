#include "cpp/futility/otel/otel_provider.h"
#include "cpp/futility/otel/metrics.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  // Initialize OpenTelemetry
  futility::otel::OtelConfig config;
  config.service_name = "test-service";
  config.otlp_endpoint = "http://localhost:4318/v1/metrics";
  config.export_interval = std::chrono::seconds(5);
  
  futility::otel::OtelProvider::Initialize(config);
  
  // Create metrics recorder
  futility::otel::MetricsRecorder recorder("test-service");
  
  // Record some test metrics
  recorder.RecordCounter("test_counter", 1);
  recorder.RecordCounter("test_counter", 1, {{"method", "GET"}, {"status", "200"}});
  
  recorder.RecordLatency("test_latency", std::chrono::microseconds(1500));
  recorder.RecordGauge("test_gauge", 42.0);
  
  std::cout << "Metrics recorded. They'll be sent to OTLP collector at :4318\n";
  std::cout << "Check Prometheus metrics at http://localhost:8889/metrics (via collector)\n";
  std::cout << "Sleeping for 10 seconds to allow export...\n";
  
  // Sleep to allow metrics to be exported
  std::this_thread::sleep_for(std::chrono::seconds(10));
  
  futility::otel::OtelProvider::Shutdown();
  
  std::cout << "Test completed\n";
  return 0;
}