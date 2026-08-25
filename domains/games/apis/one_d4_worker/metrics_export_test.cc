// The names WorkerMetrics puts on the wire, read back off a real exporter.
//
// metrics_test asserts through CapturingMetricsRecorder, which sees the name
// the caller passed. The SDK does not always export that name — RecordLatency
// appends _microseconds — so the double cannot answer what prom_proxy will
// find. This runs WorkerMetrics through the real MetricsRecorder and asks
// the exporter.

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

TEST_F(MetricsExportTest, TheRunDurationIsExportedUnderTheNameProm_ProxyQueries) {
  futility::otel::MetricsRecorder recorder("one_d4_worker");
  WorkerMetrics metrics(recorder);
  metrics.RunFinished(RunOutcome::kCompleted, absl::Seconds(3));

  const std::set<std::string> exported = ExportedNames();
  EXPECT_TRUE(exported.count(kRunDurationMetric) == 1)
      << "the run duration is not exported as " << kRunDurationMetric
      << "; prom_proxy queries it as " << kRunDurationMetric << "_total";
  EXPECT_TRUE(exported.count(std::string(kRunDurationMetric) + "_microseconds") == 0)
      << "the run duration is exported under a renamed instrument; prom_proxy queries "
      << kRunDurationMetric;
}

TEST_F(MetricsExportTest, NothingExportsWithAFirstObservationGap) {
  // This worker is counters only (#1452): a histogram cannot be declared at
  // zero without biasing the mean its tiles read, so any histogram in the
  // export is a series that goes dark until its first real observation.
  futility::otel::MetricsRecorder recorder("one_d4_worker");
  WorkerMetrics metrics(recorder);
  metrics.Declare();
  metrics.RunFinished(RunOutcome::kCompleted, absl::Seconds(3));
  metrics.MonthFinished("indexed", 12);

  int swept = 0;
  reader_->Collect([&](metrics_sdk::ResourceMetrics& metrics_data) {
    for (const auto& scope : metrics_data.scope_metric_data_) {
      for (const auto& metric : scope.metric_data_) {
        ++swept;
        EXPECT_NE(metric.instrument_descriptor.type_, metrics_sdk::InstrumentType::kHistogram)
            << metric.instrument_descriptor.name_
            << " is a histogram, which cannot carry a zero baseline (#1384)";
      }
    }
    return true;
  });
  // The sweep's own control: an empty export proves nothing.
  EXPECT_GE(swept, 3);
}

}  // namespace
}  // namespace one_d4_worker
