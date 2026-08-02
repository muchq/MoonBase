#include "domains/platform/libs/futility/otel/otel_provider.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/exporters/memory/in_memory_metric_data.h"
#include "opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

// The view #1286 turns on, exercised rather than described.
//
// //domains/platform/libs/otel_contract pins the three rails' bucket constants
// equal to each other, but a constant is only half the fix: on this rail
// nothing applies it except RegisterLatencyBucketView, and a constant nothing
// applies is precisely the bug that issue is about. Deleting the AddView call,
// or changing the instrument predicate so it stops matching, would leave the
// contract test green and put every C++ latency histogram back on the SDK's
// millisecond defaults.
//
// So this drives a real MeterProvider through an in-memory reader and reads
// the boundaries back off an exported histogram.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace memory_exporter = opentelemetry::exporter::memory;

constexpr const char* kScope = "otel_provider_test";

class LatencyBucketViewTest : public ::testing::Test {
 protected:
  void SetUp() override {
    data_ = std::make_shared<memory_exporter::SimpleAggregateInMemoryMetricData>();

    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());

    metrics_sdk::PeriodicExportingMetricReaderOptions options;
    // Long enough that the background thread never races the explicit
    // ForceFlush below; the flush is what actually drives the export.
    options.export_interval_millis = std::chrono::milliseconds(60000);
    options.export_timeout_millis = std::chrono::milliseconds(5000);
    provider_->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        memory_exporter::InMemoryMetricExporterFactory::Create(data_), options));
  }

  /// Records one observation into a histogram of this name and returns the
  /// bucket boundaries the SDK actually aggregated it into.
  std::vector<double> BoundariesFor(const std::string& instrument_name) {
    auto meter = provider_->GetMeter(kScope, "1.0.0");
    auto histogram = meter->CreateUInt64Histogram(instrument_name);
    histogram->Record(1, opentelemetry::context::Context{});
    provider_->ForceFlush();

    const auto& series = data_->Get(kScope, instrument_name);
    EXPECT_FALSE(series.empty()) << instrument_name << " exported no data point";
    if (series.empty()) return {};

    const auto& point = series.begin()->second;
    const auto* histogram_point =
        opentelemetry::nostd::get_if<metrics_sdk::HistogramPointData>(&point);
    EXPECT_NE(histogram_point, nullptr) << instrument_name << " did not export as a histogram";
    if (histogram_point == nullptr) return {};

    return histogram_point->boundaries_;
  }

  std::shared_ptr<memory_exporter::SimpleAggregateInMemoryMetricData> data_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

TEST_F(LatencyBucketViewTest, AMicrosecondsHistogramGetsTheExplicitBounds) {
  RegisterLatencyBucketView(*provider_);

  const std::vector<double> expected(kHttpLatencyBucketBoundsMicros.begin(),
                                     kHttpLatencyBucketBoundsMicros.end());
  EXPECT_EQ(BoundariesFor("http_server_request_duration_microseconds"), expected);
}

// The suffix match is the whole predicate, so the other instrument that carries
// it has to be covered by name. portrait's render timing sits near a second and
// was capped at the same 10ms as the HTTP family until this view existed
// (#1287) — a predicate narrowed to the http_server_ prefix would pass the test
// above and silently re-break this one.
TEST_F(LatencyBucketViewTest, PortraitsRenderTimerIsCoveredByTheSamePredicate) {
  RegisterLatencyBucketView(*provider_);

  const std::vector<double> expected(kHttpLatencyBucketBoundsMicros.begin(),
                                     kHttpLatencyBucketBoundsMicros.end());
  EXPECT_EQ(BoundariesFor("trace_request_duration_microseconds"), expected);
}

// And the negative half. RecordDistribution keeps whatever bare name it is
// given, and those instruments count spheres and lights and rows — quantities
// in the single digits, which a layout starting at 100µs would collapse into
// one bucket. A predicate widened to "every histogram" passes both tests above
// and quietly ruins these.
TEST_F(LatencyBucketViewTest, ADistributionKeepsTheDefaultLayout) {
  RegisterLatencyBucketView(*provider_);

  const std::vector<double> latency(kHttpLatencyBucketBoundsMicros.begin(),
                                    kHttpLatencyBucketBoundsMicros.end());
  const std::vector<double> actual = BoundariesFor("scene_sphere_count");

  EXPECT_FALSE(actual.empty());
  EXPECT_NE(actual, latency)
      << "scene_sphere_count was caught by the latency view; a scene with three spheres now "
         "lands in the same bucket as one with ninety";
}

// The control that makes the three above mean something: without the view, the
// microseconds histogram gets the SDK defaults. If this ever starts failing,
// the SDK changed its defaults and the assertions above may be passing for a
// reason other than the view.
TEST_F(LatencyBucketViewTest, WithoutTheViewTheSameInstrumentGetsTheSdkDefaults) {
  const std::vector<double> latency(kHttpLatencyBucketBoundsMicros.begin(),
                                    kHttpLatencyBucketBoundsMicros.end());
  const std::vector<double> actual = BoundariesFor("http_server_request_duration_microseconds");

  EXPECT_FALSE(actual.empty());
  EXPECT_NE(actual, latency)
      << "the explicit bounds appeared without RegisterLatencyBucketView being called, so the "
         "other tests in this file prove nothing about it";
}

// Everything above drives RegisterLatencyBucketView directly, which leaves the
// call site itself unpinned: deleting the one line in OtelProvider's
// constructor puts production back on the SDK defaults with all four of those
// tests still green. This closes that by going through the constructor and
// reading the view off the provider it publishes globally.
TEST(OtelProviderWiringTest, TheConstructorRegistersTheView) {
  auto data = std::make_shared<memory_exporter::SimpleAggregateInMemoryMetricData>();

  OtelConfig config;
  config.service_name = "otel_provider_wiring_test";
  config.enable_metrics = true;
  // Nothing listens here. The OTLP reader the constructor installs is flushed
  // alongside ours and fails to connect, which is fine — the export under test
  // is the in-memory one. A port that refuses immediately keeps that failure
  // from costing the export timeout.
  config.otlp_endpoint = "http://127.0.0.1:1/v1/metrics";
  config.export_interval = std::chrono::seconds(3600);

  OtelProvider provider(config);
  auto published =
      std::static_pointer_cast<metrics_sdk::MeterProvider>(provider.GetMeterProvider());
  ASSERT_NE(published, nullptr);

  metrics_sdk::PeriodicExportingMetricReaderOptions options;
  options.export_interval_millis = std::chrono::milliseconds(60000);
  options.export_timeout_millis = std::chrono::milliseconds(1000);
  published->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
      memory_exporter::InMemoryMetricExporterFactory::Create(data), options));

  auto meter = published->GetMeter(kScope, "1.0.0");
  auto histogram = meter->CreateUInt64Histogram("http_server_request_duration_microseconds");
  histogram->Record(1, opentelemetry::context::Context{});
  published->ForceFlush();

  const auto& series = data->Get(kScope, "http_server_request_duration_microseconds");
  ASSERT_FALSE(series.empty()) << "the provider exported no data point";
  const auto* point =
      opentelemetry::nostd::get_if<metrics_sdk::HistogramPointData>(&series.begin()->second);
  ASSERT_NE(point, nullptr);

  const std::vector<double> expected(kHttpLatencyBucketBoundsMicros.begin(),
                                     kHttpLatencyBucketBoundsMicros.end());
  EXPECT_EQ(point->boundaries_, expected)
      << "OtelProvider built a provider without the latency view, so every service on this rail "
         "is back on the SDK's millisecond defaults (#1286)";
}

}  // namespace
}  // namespace futility::otel
