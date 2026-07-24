#include "domains/platform/libs/futility/otel/metrics.h"

#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/provider.h"

namespace futility::otel {

MetricsRecorder::MetricsRecorder(const std::string& service_name) {
  auto provider = opentelemetry::v1::metrics::Provider::GetMeterProvider();
  meter_ = provider->GetMeter(service_name, "1.0.0");
}

template <typename Instrument, typename Factory>
Instrument* MetricsRecorder::FindOrCreate(
    std::unordered_map<std::string, std::unique_ptr<Instrument>>& cache,
    const std::string& metric_name, const Factory& make) {
  const std::lock_guard<std::mutex> lock(mu_);
  auto it = cache.find(metric_name);
  if (it == cache.end()) {
    it = cache.emplace(metric_name, make()).first;
  }
  return it->second.get();
}

void MetricsRecorder::RecordCounter(const std::string& metric_name, int64_t value,
                                    const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto* counter = FindOrCreate(counters_, metric_name,
                               [&] { return meter_->CreateUInt64Counter(metric_name); });
  if (counter != nullptr) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      counter->Add(static_cast<uint64_t>(value), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      counter->Add(static_cast<uint64_t>(value), kv_iterable, context);
    }
  }
}

void MetricsRecorder::RecordLatency(const std::string& metric_name,
                                    std::chrono::microseconds duration,
                                    const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto* histogram = FindOrCreate(histograms_, metric_name, [&] {
    return meter_->CreateUInt64Histogram(metric_name + "_microseconds");
  });
  if (histogram != nullptr) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      histogram->Record(static_cast<uint64_t>(duration.count()), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      histogram->Record(static_cast<uint64_t>(duration.count()), kv_iterable, context);
    }
  }
}

void MetricsRecorder::RecordDistribution(const std::string& metric_name, double value,
                                         const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto* histogram = FindOrCreate(histograms_, metric_name,
                                 [&] { return meter_->CreateUInt64Histogram(metric_name); });
  if (histogram != nullptr) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      histogram->Record(static_cast<uint64_t>(value), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      histogram->Record(static_cast<uint64_t>(value), kv_iterable, context);
    }
  }
}

void MetricsRecorder::RecordGauge(const std::string& metric_name, double value,
                                  const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto* gauge = FindOrCreate(gauges_, metric_name, [&] {
    return meter_->CreateInt64UpDownCounter(metric_name + "_gauge");
  });
  if (gauge != nullptr) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      gauge->Add(static_cast<int64_t>(value), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      gauge->Add(static_cast<int64_t>(value), kv_iterable, context);
    }
  }
}

}  // namespace futility::otel