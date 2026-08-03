#include "domains/platform/libs/futility/otel/http_instrument_descriptions.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "opentelemetry/exporters/memory/in_memory_metric_data.h"
#include "opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"

// The descriptions this rail exports, exercised rather than declared.
//
// //domains/platform/libs/otel_contract pins the table in
// http_instrument_descriptions.h equal to yodel's and server_pal's, but it
// reads the header as text — a table nothing applies would satisfy it exactly
// as well as one that reaches the collector. Two things stand between the
// table and the wire, and both have been wrong: MetricsRecorder creates every
// instrument and did so with no description at all, and RegisterLatencyBucketView
// matches every *_microseconds histogram and used to stamp its own sentence
// over whatever the instrument declared.
//
// So this drives a real MeterProvider, records through the recorder services
// actually use, and reads the description back off the exported instrument
// descriptor.

namespace futility::otel {
namespace {

namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace memory_exporter = opentelemetry::exporter::memory;

class ExportedDescriptionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    data_ = std::make_shared<memory_exporter::CircularBufferInMemoryMetricData>(64);

    auto provider = metrics_sdk::MeterProviderFactory::Create();
    provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(provider.release());

    metrics_sdk::PeriodicExportingMetricReaderOptions options;
    // Long enough that the background thread never races the explicit
    // ForceFlush below; the flush is what actually drives the export.
    options.export_interval_millis = std::chrono::milliseconds(60000);
    options.export_timeout_millis = std::chrono::milliseconds(5000);
    provider_->AddMetricReader(metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
        memory_exporter::InMemoryMetricExporterFactory::Create(data_), options));

    // The view is part of what is under test here, not scaffolding: it is the
    // half of this that used to overwrite descriptions.
    RegisterLatencyBucketView(*provider_);

    // MetricsRecorder reads the global provider, which is how every service
    // reaches it too.
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(provider_));
  }

  void TearDown() override {
    // The no-op provider, not an empty pointer: GetMeterProvider() is
    // documented never to return nullptr, and MetricsRecorder's constructor
    // dereferences it. Clearing it would leave any later test in this binary
    // one MetricsRecorder away from a segfault.
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::metrics::MeterProvider>(
            new opentelemetry::metrics::NoopMeterProvider()));
  }

  /// The descriptor the SDK exported for `instrument_name`, after everything —
  /// instrument creation and the view — has had its say.
  ///
  /// Returns the whole descriptor rather than the description alone so that an
  /// instrument that was never exported is distinguishable from one exported
  /// with an empty description; the tests below assert both cases.
  metrics_sdk::InstrumentDescriptor ExportedDescriptorFor(const std::string& instrument_name) {
    provider_->ForceFlush();

    bool found = false;
    metrics_sdk::InstrumentDescriptor descriptor;
    for (const auto& resource_metrics : data_->Get()) {
      for (const auto& scope : resource_metrics->scope_metric_data_) {
        for (const auto& metric : scope.metric_data_) {
          if (metric.instrument_descriptor.name_ != instrument_name) continue;
          found = true;
          descriptor = metric.instrument_descriptor;
        }
      }
    }

    EXPECT_TRUE(found) << instrument_name << " was never exported";
    return descriptor;
  }

  std::string ExportedDescriptionFor(const std::string& instrument_name) {
    return ExportedDescriptorFor(instrument_name).description_;
  }

  std::shared_ptr<memory_exporter::CircularBufferInMemoryMetricData> data_;
  std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

// Every shared counter, not just the first one. The cross-language pin in
// otel_contract reads this rail's table as text, so it cannot tell a table
// entry from a sentence about one; these are the assertions that require the
// table to have actually reached an exported instrument. _success and _failure
// were the two that nothing here covered, and _success is the instrument the
// en-dash conflict was about.
TEST_F(ExportedDescriptionTest, EverySharedCounterCarriesItsCanonicalDescription) {
  MetricsRecorder recorder("test-service");
  recorder.RecordCounter("http_server_requests", 1);
  recorder.RecordCounter("http_server_requests_success", 1);
  recorder.RecordCounter("http_server_requests_failure", 1);

  EXPECT_EQ(ExportedDescriptionFor("http_server_requests"), "HTTP requests received");
  EXPECT_EQ(ExportedDescriptionFor("http_server_requests_success"),
            "HTTP requests completed successfully (2xx-3xx)");
  EXPECT_EQ(ExportedDescriptionFor("http_server_requests_failure"),
            "HTTP requests that returned 4xx or 5xx");
}

// The unit is the same contract as the description and fails far more quietly.
// The collector's Prometheus exporter folds a non-empty unit into the metric
// NAME (http_server_requests_total becomes
// http_server_requests_microseconds_total), so a rail that sets one does not
// log a conflict — it silently forks the series and drops off every prom_proxy
// panel, which selects by literal name. All three rails leave it empty today;
// this pins that for the one rail whose instrument creation this commit
// touched.
TEST_F(ExportedDescriptionTest, TheSharedInstrumentsDeclareNoUnit) {
  MetricsRecorder recorder("test-service");
  recorder.RecordCounter("http_server_requests", 1);
  recorder.RecordGauge("http_server_requests_active", 1);
  recorder.RecordLatency("http_server_request_duration", std::chrono::microseconds(1));

  for (const auto& name : {"http_server_requests", "http_server_requests_active_gauge",
                           "http_server_request_duration_microseconds"}) {
    EXPECT_EQ(ExportedDescriptorFor(name).unit_, "")
        << name << " declares a unit; the collector folds it into the metric name";
  }
}

// The gauge's exported name is not the name its caller passes: RecordGauge
// appends _gauge. Looking the description up under the stem finds nothing and
// exports an empty one, which conflicts with yodel just as loudly as a wrong
// sentence would.
TEST_F(ExportedDescriptionTest, TheActiveGaugeIsDescribedUnderItsSuffixedName) {
  MetricsRecorder recorder("test-service");
  recorder.RecordGauge("http_server_requests_active", 1);

  EXPECT_EQ(ExportedDescriptionFor("http_server_requests_active_gauge"),
            "HTTP requests currently in flight");
}

// The regression that produced the collector's log flood. The latency view
// matches this instrument, so whatever it says about descriptions lands here.
TEST_F(ExportedDescriptionTest, TheLatencyHistogramKeepsItsDescriptionThroughTheBucketView) {
  MetricsRecorder recorder("test-service");
  recorder.RecordLatency("http_server_request_duration", std::chrono::microseconds(1));

  EXPECT_EQ(ExportedDescriptionFor("http_server_request_duration_microseconds"),
            "HTTP request duration in microseconds");
}

// And the other side of the same view: it matches every *_microseconds
// histogram, including ones no other service reports. A description set on the
// view rather than the instrument is applied to all of them indiscriminately,
// which is how portrait's render timer came to be labelled as HTTP latency.
TEST_F(ExportedDescriptionTest, AServiceDefinedLatencyIsLeftUndescribed) {
  MetricsRecorder recorder("test-service");
  recorder.RecordLatency("trace_request_duration", std::chrono::microseconds(1));

  EXPECT_EQ(ExportedDescriptionFor("trace_request_duration_microseconds"), "");
}

}  // namespace
}  // namespace futility::otel
