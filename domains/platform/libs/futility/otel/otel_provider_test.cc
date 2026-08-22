#include "domains/platform/libs/futility/otel/otel_provider.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "domains/platform/libs/futility/otel/collect_on_demand_reader.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
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
// So this drives a real MeterProvider and reads the boundaries back off a
// collected histogram.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;

constexpr const char* kScope = "otel_provider_test";

/// The bucket boundaries `reader` reports for this instrument, or an empty
/// vector with a failure already recorded if it reports no histogram under
/// that name.
std::vector<double> BoundariesOf(metrics_sdk::MetricReader& reader,
                                 const std::string& instrument_name) {
  bool found = false;
  bool as_histogram = true;
  std::vector<double> boundaries;

  reader.Collect([&](metrics_sdk::ResourceMetrics& metrics) {
    for (const auto& scope : metrics.scope_metric_data_) {
      if (scope.scope_ == nullptr || scope.scope_->GetName() != kScope) continue;
      for (const auto& metric : scope.metric_data_) {
        if (metric.instrument_descriptor.name_ != instrument_name) continue;
        for (const auto& point : metric.point_data_attr_) {
          found = true;
          const auto* histogram_point =
              opentelemetry::nostd::get_if<metrics_sdk::HistogramPointData>(&point.point_data);
          if (histogram_point == nullptr) {
            as_histogram = false;
            continue;
          }
          boundaries = histogram_point->boundaries_;
        }
      }
    }
    return true;
  });

  EXPECT_TRUE(found) << instrument_name << " reported no data point";
  EXPECT_TRUE(as_histogram) << instrument_name << " did not aggregate as a histogram";
  return boundaries;
}

class LatencyBucketViewTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());

    reader_ = std::make_shared<CollectOnDemandReader>();
    provider_->AddMetricReader(reader_);
  }

  /// Records one observation into a histogram of this name and returns the
  /// bucket boundaries the SDK actually aggregated it into.
  std::vector<double> BoundariesFor(const std::string& instrument_name) {
    auto meter = provider_->GetMeter(kScope, "1.0.0");
    auto histogram = meter->CreateUInt64Histogram(instrument_name);
    histogram->Record(1, opentelemetry::context::Context{});

    return BoundariesOf(*reader_, instrument_name);
  }

  std::shared_ptr<CollectOnDemandReader> reader_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

TEST_F(LatencyBucketViewTest, ANamedHistogramGetsTheBoundsItAskedFor) {
  // A distribution whose range is nothing like an HTTP request's: on the
  // SDK defaults every observation lands in +Inf, and histogram_quantile
  // answers the highest finite bound forever.
  const std::vector<double> bounds = {1'000, 60'000'000, 3'600'000'000};
  RegisterHistogramBounds(*provider_, "index_run_duration_micros", bounds);

  EXPECT_EQ(BoundariesFor("index_run_duration_micros"), bounds);
}

TEST_F(LatencyBucketViewTest, NamedBoundsClaimOnlyTheirOwnInstrument) {
  // The selector is a regex. Unanchored, this would also claim
  // index_run_duration_micros_by_player and anything else containing it.
  RegisterHistogramBounds(*provider_, "run_seconds", {1, 2, 3});

  const std::vector<double> neighbour = BoundariesFor("run_seconds_extra");
  EXPECT_NE(neighbour, (std::vector<double>{1, 2, 3}));
  EXPECT_FALSE(neighbour.empty()) << "the neighbour reported nothing to compare";
}

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
  OtelConfig config;
  config.service_name = "otel_provider_wiring_test";
  config.enable_metrics = true;
  // Nothing listens here, and nothing needs to: the reader added below
  // collects in process and the constructor's own OTLP reader is not what is
  // under test. A port that refuses immediately keeps that reader's flush on
  // the way out from costing the export timeout.
  config.otlp_endpoint = "http://127.0.0.1:1/v1/metrics";
  config.export_interval = std::chrono::seconds(3600);

  OtelProvider provider(config);
  auto published =
      std::static_pointer_cast<metrics_sdk::MeterProvider>(provider.GetMeterProvider());
  ASSERT_NE(published, nullptr);

  auto reader = std::make_shared<CollectOnDemandReader>();
  published->AddMetricReader(reader);

  auto meter = published->GetMeter(kScope, "1.0.0");
  auto histogram = meter->CreateUInt64Histogram("http_server_request_duration_microseconds");
  histogram->Record(1, opentelemetry::context::Context{});

  const std::vector<double> expected(kHttpLatencyBucketBoundsMicros.begin(),
                                     kHttpLatencyBucketBoundsMicros.end());
  EXPECT_EQ(BoundariesOf(*reader, "http_server_request_duration_microseconds"), expected)
      << "OtelProvider built a provider without the latency view, so every service on this rail "
         "is back on the SDK's millisecond defaults (#1286)";
}

}  // namespace
}  // namespace futility::otel
