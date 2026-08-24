// The names WorkerMetrics puts on the wire, read back off a real exporter.
//
// metrics_test asserts through CapturingMetricsRecorder, which sees the name
// the caller passed. The SDK does not always export that name — RecordLatency
// appends _microseconds — so the double cannot answer what prom_proxy will
// find, and the registered histogram bounds are keyed on the exported name.
// This runs WorkerMetrics through the real MetricsRecorder and asks the
// exporter.

#include <chrono>
#include <memory>
#include <set>
#include <string>

#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/platform/libs/futility/otel/collect_on_demand_reader.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

namespace one_d4_worker {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;
using futility::otel::CollectOnDemandReader;

class MetricsExportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());
    reader_ = std::make_shared<CollectOnDemandReader>();
    provider_->AddMetricReader(reader_);

    // MetricsRecorder reads the global provider, which is how the worker
    // reaches it too.
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(provider_));
  }

  void TearDown() override {
    // The no-op provider, not an empty pointer: GetMeterProvider() never
    // returns nullptr and MetricsRecorder's constructor dereferences it.
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(
            new opentelemetry::metrics::NoopMeterProvider()));
  }

  /// Every instrument name the exporter reports.
  std::set<std::string> ExportedNames() {
    std::set<std::string> names;
    reader_->Collect([&](metrics_sdk::ResourceMetrics& metrics) {
      for (const auto& scope : metrics.scope_metric_data_) {
        for (const auto& metric : scope.metric_data_) {
          names.insert(metric.instrument_descriptor.name_);
        }
      }
      return true;
    });
    return names;
  }

  std::shared_ptr<CollectOnDemandReader> reader_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

TEST_F(MetricsExportTest, TheRunDurationIsExportedUnderTheNameItsBucketsAreRegisteredFor) {
  futility::otel::MetricsRecorder recorder("one_d4_worker");
  WorkerMetrics metrics(recorder);
  metrics.RunFinished(RunOutcome::kCompleted, absl::Seconds(3));

  const std::set<std::string> exported = ExportedNames();
  EXPECT_TRUE(exported.count(kRunDurationMetric) == 1)
      << "the run duration is not exported as " << kRunDurationMetric
      << ", so HistogramBounds registers buckets for an instrument nobody writes";
  EXPECT_TRUE(exported.count(std::string(kRunDurationMetric) + "_microseconds") == 0)
      << "the run duration is exported under a renamed instrument; prom_proxy queries "
      << kRunDurationMetric << " and the registered buckets key on it";
}

}  // namespace
}  // namespace one_d4_worker
