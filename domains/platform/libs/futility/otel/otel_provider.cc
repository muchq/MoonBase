#include "domains/platform/libs/futility/otel/otel_provider.h"

#include <cstdlib>

#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/meter_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/view_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/semconv/service_attributes.h"

namespace futility::otel {

namespace {

// The only way to give a histogram explicit bounds in opentelemetry-cpp: the
// API's CreateUInt64Histogram takes a name and nothing else, so a view on the
// provider is what turns kHttpLatencyBucketBoundsMicros into the layout the
// SDK actually aggregates into. Without it every histogram silently gets the
// millisecond-shaped SDK defaults (#1286).
//
// Matched on the name suffix rather than on http_server_request_duration_
// microseconds alone. MetricsRecorder::RecordLatency appends _microseconds to
// whatever it is handed and is the only thing in the codebase that produces
// that suffix, so every instrument it mints has the same unit and the same
// defect — portrait's trace_request_duration_microseconds sits near a second
// per render and was capped at the same 10ms as the HTTP family.
// RecordDistribution keeps the bare name it was given, so scene_sphere_count
// and its kin are not caught by this and keep the default layout, which is
// what they want.
void RegisterLatencyBucketView(opentelemetry::sdk::metrics::MeterProvider& meter_provider) {
  auto histogram_config =
      std::make_shared<opentelemetry::sdk::metrics::HistogramAggregationConfig>();
  histogram_config->boundaries_.assign(kHttpLatencyBucketBoundsMicros.begin(),
                                       kHttpLatencyBucketBoundsMicros.end());

  auto instrument_selector = opentelemetry::sdk::metrics::InstrumentSelectorFactory::Create(
      opentelemetry::sdk::metrics::InstrumentType::kHistogram, ".*_microseconds",
      /*unit=*/"");

  // Empty name/version/schema each match everything: MeterSelector builds
  // exact-match predicates, and PredicateFactory maps an empty exact pattern
  // to MatchEverything. It has to match everything here — MetricsRecorder
  // names its meter after the service, so there is no one meter name to pin.
  auto meter_selector = opentelemetry::sdk::metrics::MeterSelectorFactory::Create(
      /*name=*/"", /*version=*/"", /*schema=*/"");

  // Empty view name keeps the instrument's own name (meter.cc only overrides
  // when the view names something). Renaming the stream here would point every
  // service's duration series at a name prom_proxy does not query.
  auto view = opentelemetry::sdk::metrics::ViewFactory::Create(
      /*name=*/"", "Latency in microseconds, bucketed for 100us to 10s",
      opentelemetry::sdk::metrics::AggregationType::kHistogram, histogram_config);

  meter_provider.AddView(std::move(instrument_selector), std::move(meter_selector),
                         std::move(view));
}

}  // namespace

OtelProvider::OtelProvider(const OtelConfig& config) : metrics_enabled_(config.enable_metrics) {
  if (!config.enable_metrics) {
    return;
  }

  // Get OTLP endpoint from environment variable or use config default
  std::string otlp_endpoint = config.otlp_endpoint;
  if (const char* env_endpoint = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT")) {
    otlp_endpoint = std::string(env_endpoint) + "/v1/metrics";
  }

  // Create resource with service information
  auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{opentelemetry::semconv::service::kServiceName, config.service_name},
       {opentelemetry::semconv::service::kServiceVersion, config.service_version}});

  // Create OTLP HTTP metric exporter
  opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions otlp_options;
  otlp_options.url = otlp_endpoint;
  otlp_options.content_type = opentelemetry::exporter::otlp::HttpRequestContentType::kJson;

  auto otlp_exporter =
      opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(otlp_options);

  // Create meter provider
  auto provider = opentelemetry::sdk::metrics::MeterProviderFactory::Create();
  auto meter_provider =
      std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>(provider.release());

  // Create periodic metric reader with OTLP exporter
  opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions reader_options;
  reader_options.export_interval_millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(config.export_interval);
  reader_options.export_timeout_millis = std::chrono::milliseconds(5000);

  auto reader = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
      std::move(otlp_exporter), reader_options);

  // Add metric reader to meter provider
  meter_provider->AddMetricReader(std::move(reader));

  RegisterLatencyBucketView(*meter_provider);

  // Set global meter provider
  meter_provider_ = std::move(meter_provider);
  opentelemetry::metrics::Provider::SetMeterProvider(meter_provider_);
}

OtelProvider::~OtelProvider() {
  if (metrics_enabled_ && meter_provider_) {
    // Reset global provider to default
    opentelemetry::metrics::Provider::SetMeterProvider(
        std::shared_ptr<opentelemetry::v1::metrics::MeterProvider>{});
    meter_provider_.reset();
  }
}

std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> OtelProvider::GetMeterProvider() const {
  return meter_provider_;
}

}  // namespace futility::otel