#include <gtest/gtest.h>

#include <map>
#include <string>

#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"
#include "domains/platform/libs/futility/otel/metrics.h"

// The zero baseline (#1323). Instruments here are created lazily on the first
// RecordCounter, so without a declaration a counter's first exported sample is
// its first event's value — and increase() has nothing earlier to measure it
// against, which makes that event invisible for good. Declaring records a zero
// so the series exists before anything happens.

namespace futility::otel {
namespace {

TEST(MetricsDeclarationTest, DeclareCounterRecordsAZeroForTheSeries) {
  CapturingMetricsRecorder metrics;
  metrics.DeclareCounter("trace_requests_completed");

  EXPECT_EQ(metrics.CounterTotal("trace_requests_completed"), 0);
  // Presence, not just a zero read: CounterTotal sums an empty match to 0, so
  // the assertion above passes just as happily against a recorder that
  // declared nothing at all.
  ASSERT_EQ(metrics.Entries().size(), 1U);
  EXPECT_EQ(metrics.Entries()[0].name, "trace_requests_completed");
  EXPECT_EQ(metrics.Entries()[0].value, 0);
}

TEST(MetricsDeclarationTest, DeclareCounterCarriesItsLabelSet) {
  CapturingMetricsRecorder metrics;
  metrics.DeclareCounter("trace_requests_failed", {{"error", "out_of_memory"}});

  // The declared series is the labelled one, not a bare name beside it — a
  // baseline under the wrong labels leaves the real series unbaselined.
  EXPECT_EQ(metrics.CounterTotal("trace_requests_failed", {{"error", "out_of_memory"}}), 0);
  ASSERT_EQ(metrics.Entries().size(), 1U);
  EXPECT_EQ(metrics.Entries()[0].attributes,
            (std::map<std::string, std::string>{{"error", "out_of_memory"}}));
}

TEST(MetricsDeclarationTest, DeclaringDoesNotDisturbLaterCounting) {
  CapturingMetricsRecorder metrics;
  metrics.DeclareCounter("trace_requests_total");
  metrics.RecordCounter("trace_requests_total", 1, {});
  metrics.RecordCounter("trace_requests_total", 1, {});

  EXPECT_EQ(metrics.CounterTotal("trace_requests_total"), 2);
}

}  // namespace
}  // namespace futility::otel
