#include "cpp/futility/otel/metrics.h"

#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/metrics/provider.h"

namespace futility::otel {

MetricsRecorder::MetricsRecorder(const std::string& service_name) {
  auto provider = opentelemetry::v1::metrics::Provider::GetMeterProvider();
  meter_ = provider->GetMeter(service_name, "1.0.0");
}

void MetricsRecorder::RecordCounter(const std::string& metric_name, int64_t value,
                                    const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto it = counters_.find(metric_name);
  if (it == counters_.end()) {
    counters_[metric_name] = meter_->CreateUInt64Counter(metric_name);
    it = counters_.find(metric_name);
  }

  if (it->second) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      it->second->Add(static_cast<uint64_t>(value), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      it->second->Add(static_cast<uint64_t>(value), kv_iterable, context);
    }
  }
}

void MetricsRecorder::RecordLatency(const std::string& metric_name,
                                    std::chrono::microseconds duration,
                                    const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto it = histograms_.find(metric_name);
  if (it == histograms_.end()) {
    histograms_[metric_name] = meter_->CreateUInt64Histogram(metric_name + "_microseconds");
    it = histograms_.find(metric_name);
  }

  if (it->second) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      it->second->Record(static_cast<uint64_t>(duration.count()), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      it->second->Record(static_cast<uint64_t>(duration.count()), kv_iterable, context);
    }
  }
}

void MetricsRecorder::RecordGauge(const std::string& metric_name, double value,
                                  const std::map<std::string, std::string>& attributes) {
  if (!meter_) return;

  auto it = gauges_.find(metric_name);
  if (it == gauges_.end()) {
    gauges_[metric_name] = meter_->CreateInt64UpDownCounter(metric_name + "_gauge");
    it = gauges_.find(metric_name);
  }

  if (it->second) {
    auto context = opentelemetry::context::Context{};
    if (attributes.empty()) {
      it->second->Add(static_cast<int64_t>(value), context);
    } else {
      // Convert attributes to OpenTelemetry format
      std::vector<std::pair<std::string, std::string>> attr_vec(attributes.begin(),
                                                                attributes.end());
      auto kv_iterable = opentelemetry::common::KeyValueIterableView<
          std::vector<std::pair<std::string, std::string>>>(attr_vec);
      it->second->Add(static_cast<int64_t>(value), kv_iterable, context);
    }
  }
}

}  // namespace futility::otel