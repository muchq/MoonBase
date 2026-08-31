#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/collect_on_demand_reader.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

// Every exported point carries a service_name attribute. prom_proxy scopes
// every custom-metric query with service_name=~"...", and the collector's
// prometheus exporter does not turn the OTLP resource into point labels — so
// a point without this attribute is invisible to every dashboard tile that
// queries it.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;

class ServiceNameLabelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());
    reader_ = std::make_shared<CollectOnDemandReader>();
    provider_->AddMetricReader(reader_);
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(provider_));
  }

  void TearDown() override {
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(
            new opentelemetry::metrics::NoopMeterProvider()));
  }

  /// service_name attribute per exported point, keyed "metric/point-index";
  /// a point without one maps to "<absent>".
  std::map<std::string, std::string> ServiceNamePerPoint() {
    std::map<std::string, std::string> out;
    reader_->Collect([&](metrics_sdk::ResourceMetrics& metrics) {
      for (const auto& scope : metrics.scope_metric_data_) {
        for (const auto& metric : scope.metric_data_) {
          int index = 0;
          for (const auto& point : metric.point_data_attr_) {
            const std::string key =
                metric.instrument_descriptor.name_ + "/" + std::to_string(index++);
            auto it = point.attributes.find("service_name");
            if (it == point.attributes.end()) {
              out[key] = "<absent>";
            } else if (const auto* value = opentelemetry::nostd::get_if<std::string>(&it->second)) {
              out[key] = *value;
            } else {
              out[key] = "<non-string>";
            }
          }
        }
      }
      return true;
    });
    return out;
  }

  std::shared_ptr<CollectOnDemandReader> reader_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

TEST_F(ServiceNameLabelTest, EveryPointCarriesTheServiceName) {
  MetricsRecorder recorder("stamp_test_service");
  recorder.RecordCounter("bare_counter");
  recorder.RecordCounter("labelled_counter", 2, {{"outcome", "ok"}});
  recorder.RecordLatency("some_latency", std::chrono::microseconds(1200));
  recorder.RecordDistribution("some_distribution", 3.0);
  recorder.RecordGauge("some_gauge", 4.0);

  const auto points = ServiceNamePerPoint();
  ASSERT_GE(points.size(), 5U) << "the export is missing points; the sweep proves nothing";
  for (const auto& [point, service_name] : points) {
    EXPECT_EQ(service_name, "stamp_test_service") << point;
  }
}

TEST_F(ServiceNameLabelTest, ACallerSuppliedServiceNameIsOverwritten) {
  MetricsRecorder recorder("stamp_test_service");
  recorder.RecordCounter("labelled_counter", 1, {{"service_name", "impostor"}});

  for (const auto& [point, service_name] : ServiceNamePerPoint()) {
    EXPECT_EQ(service_name, "stamp_test_service") << point;
  }
}

}  // namespace
}  // namespace futility::otel
