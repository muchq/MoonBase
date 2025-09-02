#include "cpp/futility/otel/otel_provider.h"

#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/semconv/service_attributes.h"

namespace futility::otel {

std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> OtelProvider::meter_provider_;
bool OtelProvider::initialized_ = false;

void OtelProvider::Initialize(const OtelConfig& config) {
  if (initialized_) {
    return;
  }

  if (!config.enable_metrics) {
    initialized_ = true;
    return;
  }

  // Create resource with service information
  auto resource = opentelemetry::sdk::resource::Resource::Create({
    {opentelemetry::semconv::service::kServiceName, config.service_name},
    {opentelemetry::semconv::service::kServiceVersion, config.service_version}
  });

  // Create OTLP HTTP metric exporter
  opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions otlp_options;
  otlp_options.url = config.otlp_endpoint;
  otlp_options.content_type = opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
  
  auto otlp_exporter = opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(otlp_options);

  // Create meter provider
  auto provider = opentelemetry::sdk::metrics::MeterProviderFactory::Create();
  auto meter_provider = std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>(provider.release());
  
  // Create periodic metric reader with OTLP exporter
  opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions reader_options;
  reader_options.export_interval_millis = std::chrono::duration_cast<std::chrono::milliseconds>(config.export_interval);
  reader_options.export_timeout_millis = std::chrono::milliseconds(5000);
  
  auto reader = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
    std::move(otlp_exporter), reader_options);
  
  // Add metric reader to meter provider
  meter_provider->AddMetricReader(std::move(reader));

  // Set global meter provider
  meter_provider_ = std::move(meter_provider);
  opentelemetry::metrics::Provider::SetMeterProvider(meter_provider_);
  
  initialized_ = true;
}

std::shared_ptr<opentelemetry::v1::metrics::MeterProvider> OtelProvider::GetMeterProvider() {
  return meter_provider_;
}

void OtelProvider::Shutdown() {
  if (meter_provider_) {
    meter_provider_.reset();
  }
  initialized_ = false;
}

} // namespace futility::otel