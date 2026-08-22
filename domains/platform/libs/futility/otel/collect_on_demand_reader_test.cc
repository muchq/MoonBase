#include "domains/platform/libs/futility/otel/collect_on_demand_reader.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

// The two properties every test on this reader leans on, pinned here so a
// change to either fails once and by name rather than as a puzzle somewhere
// downstream.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;

constexpr const char* kScope = "collect_on_demand_reader_test";

/// Whether `reader` reports an instrument of this name, collecting once.
bool Reports(metrics_sdk::MetricReader& reader, const std::string& instrument_name) {
  bool found = false;
  reader.Collect([&](metrics_sdk::ResourceMetrics& metrics) {
    for (const auto& scope : metrics.scope_metric_data_) {
      for (const auto& metric : scope.metric_data_) {
        if (metric.instrument_descriptor.name_ == instrument_name) found = true;
      }
    }
    return true;
  });
  return found;
}

class CollectOnDemandReaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());
    reader_ = std::make_shared<CollectOnDemandReader>();
    provider_->AddMetricReader(reader_);
  }

  std::shared_ptr<CollectOnDemandReader> reader_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

// The whole point: no flush, no interval, no waiting on another thread to get
// round to it. A reader that collected in the background would need a flush
// here, and the flush is the part that can hang.
TEST_F(CollectOnDemandReaderTest, CollectSeesARecordingMadeAMomentEarlier) {
  auto meter = provider_->GetMeter(kScope, "1.0.0");
  auto counter = meter->CreateUInt64Counter("index_runs");
  counter->Add(1);

  EXPECT_TRUE(Reports(*reader_, "index_runs"));
}

// Cumulative, not delta. Callers look one instrument up at a time and collect
// once per lookup; under delta the second lookup onwards would report nothing
// recorded since the first, and every test that checks more than one
// instrument would fail on all but the first.
TEST_F(CollectOnDemandReaderTest, ASecondCollectStillReportsTheInstrument) {
  auto meter = provider_->GetMeter(kScope, "1.0.0");
  auto counter = meter->CreateUInt64Counter("index_runs");
  counter->Add(1);

  ASSERT_TRUE(Reports(*reader_, "index_runs"));
  EXPECT_TRUE(Reports(*reader_, "index_runs"));
}

}  // namespace
}  // namespace futility::otel
