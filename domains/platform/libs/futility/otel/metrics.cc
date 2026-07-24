#include "domains/platform/libs/futility/otel/metrics.h"

#include <cmath>

#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/provider.h"

namespace futility::otel {

namespace {

// Every instrument records the same way: bare when there are no
// attributes, through a KeyValueIterableView otherwise. `record` is
// called with the label arguments to forward to Add/Record.
template <typename Record>
void RecordWithAttributes(const std::map<std::string, std::string>& attributes, Record&& record) {
  auto context = opentelemetry::context::Context{};
  if (attributes.empty()) {
    record(context);
  } else {
    std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(), attributes.end());
    auto kv_iterable = opentelemetry::common::KeyValueIterableView<
        std::vector<std::pair<std::string, std::string>>>(attr_vec);
    record(kv_iterable, context);
  }
}

}  // namespace

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
    RecordWithAttributes(attributes, [&](auto&&... labels) {
      counter->Add(static_cast<uint64_t>(value), std::forward<decltype(labels)>(labels)...);
    });
  }
}

void MetricsRecorder::RecordLatency(const std::string& metric_name,
                                    std::chrono::microseconds duration,
                                    const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  // Cache key is the instrument name, so a latency "x" (instrument
  // x_microseconds) can never alias a distribution "x".
  const std::string instrument_name = metric_name + "_microseconds";
  auto* histogram = FindOrCreate(histograms_, instrument_name,
                                 [&] { return meter_->CreateUInt64Histogram(instrument_name); });
  if (histogram != nullptr) {
    RecordWithAttributes(attributes, [&](auto&&... labels) {
      histogram->Record(static_cast<uint64_t>(duration.count()),
                        std::forward<decltype(labels)>(labels)...);
    });
  }
}

void MetricsRecorder::RecordDistribution(const std::string& metric_name, double value,
                                         const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;
  // The uint64 cast is UB for negative or non-finite input; observations
  // are non-negative quantities by contract, so drop anything else.
  if (!std::isfinite(value) || value < 0.0) return;

  auto* histogram = FindOrCreate(histograms_, metric_name,
                                 [&] { return meter_->CreateUInt64Histogram(metric_name); });
  if (histogram != nullptr) {
    RecordWithAttributes(attributes, [&](auto&&... labels) {
      histogram->Record(static_cast<uint64_t>(value), std::forward<decltype(labels)>(labels)...);
    });
  }
}

void MetricsRecorder::RecordGauge(const std::string& metric_name, double value,
                                  const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto* gauge = FindOrCreate(gauges_, metric_name, [&] {
    return meter_->CreateInt64UpDownCounter(metric_name + "_gauge");
  });
  if (gauge != nullptr) {
    RecordWithAttributes(attributes, [&](auto&&... labels) {
      gauge->Add(static_cast<int64_t>(value), std::forward<decltype(labels)>(labels)...);
    });
  }
}

}  // namespace futility::otel