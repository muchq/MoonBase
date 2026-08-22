#pragma once

#include <chrono>

#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

namespace futility::otel {

/// A MetricReader with no background thread: Collect() produces on the calling
/// thread, so everything the SDK has to report is in hand by the time it
/// returns.
///
/// PeriodicExportingMetricReader — the reader services run — cannot be driven
/// that way. It collects on a worker thread and ForceFlush can only ask it to:
/// the worker signals completion on a condition variable without holding the
/// mutex the flush waits under, so a signal that lands between the flush's
/// predicate check and its wait is lost. The flush then waits until its next
/// look, which the SDK schedules one export interval later. A test that picks
/// a long export interval so the worker cannot export behind its back is
/// choosing how long that wait is, and an interval past the harness timeout
/// makes the lost signal a test that hangs rather than one that fails.
///
/// Nothing about the export path is given up by not exporting: Collect() hands
/// back the same ResourceMetrics a PushMetricExporter would be handed, with
/// every registered view already applied.
class CollectOnDemandReader final : public opentelemetry::sdk::metrics::MetricReader {
 public:
  /// Cumulative, so each Collect() reports every instrument recorded so far
  /// rather than only what changed since the last one — a caller that collects
  /// twice sees the same instruments both times.
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType) const noexcept override {
    return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
  }

 private:
  bool OnForceFlush(std::chrono::microseconds) noexcept override { return true; }
  bool OnShutDown(std::chrono::microseconds) noexcept override { return true; }
};

}  // namespace futility::otel
