#include "domains/platform/libs/futility/otel/http_metrics.h"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

// The label-set and timing contract this rail shares with yodel and
// server_pal (#1304, #1305): the counters and the histogram move at
// completion carrying a bounded route, the in-flight gauge moves at start
// carrying no route, and the shared labels are spelled http_method /
// route / service_name. //domains/platform/libs/otel_contract pins the
// spellings across the three rails by reading this rail's source; these
// tests pin that the recording code actually emits them.

namespace futility::otel {
namespace {

using Attributes = std::map<std::string, std::string>;

class HttpMetricsManagerTest : public ::testing::Test {
 protected:
  HttpMetricsManagerTest() {
    auto recorder = std::make_unique<CapturingMetricsRecorder>("test_service");
    recorder_ = recorder.get();
    manager_ = std::make_unique<HttpMetricsManager>("test_service", std::move(recorder));
  }

  Attributes GaugeAttrs(const std::string& method) const {
    return {{"service_name", "test_service"}, {"http_method", method}};
  }

  Attributes BaseAttrs(const std::string& route, const std::string& method) const {
    return {{"service_name", "test_service"}, {"route", route}, {"http_method", method}};
  }

  CapturingMetricsRecorder* recorder_;
  std::unique_ptr<HttpMetricsManager> manager_;
};

// Routing hasn't happened at start, so only the gauge moves, and it carries
// no route — a route label on the gauge would make prom_proxy's negative
// probe matcher subtract in-flight probes on this rail and not the others.
TEST_F(HttpMetricsManagerTest, StartMovesOnlyTheGaugeAndWithoutARoute) {
  manager_->RecordRequestStart("GET");

  const auto entries = recorder_->Entries();
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].name, "http_server_requests_active");
  EXPECT_EQ(entries[0].value, 1);
  EXPECT_EQ(entries[0].attributes, GaugeAttrs("GET"));
  EXPECT_EQ(entries[0].attributes.count("route"), 0u);
}

// The request counter moves at completion — that is what lets it carry the
// route (#1305) — and the gauge drains with exactly the label set it was
// incremented under, or the per-label series would drift instead of
// returning to zero.
TEST_F(HttpMetricsManagerTest, CompletionRecordsTheRequestWithItsRouteAndDrainsTheGauge) {
  manager_->RecordRequestStart("GET");
  manager_->RecordRequestComplete("Trace", "GET", 200, std::chrono::microseconds(1500));

  EXPECT_EQ(recorder_->CounterTotal("http_server_requests", BaseAttrs("Trace", "GET")), 1);
  EXPECT_EQ(recorder_->CounterTotal("http_server_requests_success", BaseAttrs("Trace", "GET")), 1);

  double gauge_net = 0;
  for (const auto& entry : recorder_->Entries()) {
    if (entry.name != "http_server_requests_active") continue;
    EXPECT_EQ(entry.attributes, GaugeAttrs("GET"));
    gauge_net += entry.value;
  }
  EXPECT_EQ(gauge_net, 0);
}

// The two failure-only labels ride the failure counter, on top of the shared
// set — they are this rail's own dialect, pinned as such in otel_contract.
TEST_F(HttpMetricsManagerTest, FailureCarriesErrorTypeStatusAndResult) {
  manager_->RecordRequestComplete("Trace", "POST", 429, std::chrono::microseconds(10));

  Attributes expected = BaseAttrs("Trace", "POST");
  expected["status_code"] = "429";
  expected["result"] = "failure";
  expected["error_type"] = "rate_limited";
  EXPECT_EQ(recorder_->CounterTotal("http_server_requests_failure", expected), 1);
  EXPECT_EQ(recorder_->CounterTotal("http_server_requests_success", BaseAttrs("Trace", "POST")), 0);
}

TEST_F(HttpMetricsManagerTest, LatencyCarriesTheRouteAndTheStatusDialectLabels) {
  manager_->RecordRequestComplete("Trace", "GET", 200, std::chrono::microseconds(2500));

  bool found = false;
  for (const auto& entry : recorder_->Entries()) {
    if (entry.name != "http_server_request_duration") continue;
    found = true;
    EXPECT_EQ(entry.value, 2500);
    Attributes expected = BaseAttrs("Trace", "GET");
    expected["status_code"] = "200";
    expected["result"] = "success";
    EXPECT_EQ(entry.attributes, expected);
  }
  EXPECT_TRUE(found) << "no latency observation was recorded";
}

// The rename pin: the shared method label is spelled http_method everywhere
// (#1305 called out that futility alone spelled it `method`). A stray
// old-spelling label would fork every dashboard series for C++ services.
TEST_F(HttpMetricsManagerTest, NoInstrumentCarriesTheOldMethodSpelling) {
  manager_->RecordRequestStart("GET");
  manager_->RecordRequestComplete("Trace", "GET", 500, std::chrono::microseconds(10));

  const auto entries = recorder_->Entries();
  ASSERT_FALSE(entries.empty());
  for (const auto& entry : entries) {
    EXPECT_EQ(entry.attributes.count("method"), 0u)
        << entry.name << " carries a `method` label; the shared spelling is http_method";
    EXPECT_EQ(entry.attributes.count("http_method"), 1u)
        << entry.name << " is missing the shared http_method label";
    EXPECT_EQ(entry.attributes.count("service_name"), 1u);
  }
}

// Outcome coverage across the status space, now asserted rather than merely
// exercised: exactly one of success/failure per completion, split at 400.
TEST_F(HttpMetricsManagerTest, OutcomeSplitsAtFourHundred) {
  const auto duration = std::chrono::microseconds(1000);
  for (const int status : {200, 201, 204, 301, 302, 399}) {
    manager_->RecordRequestComplete("Trace", "GET", status, duration);
  }
  for (const int status : {400, 401, 404, 429, 500, 503}) {
    manager_->RecordRequestComplete("Trace", "GET", status, duration);
  }

  EXPECT_EQ(recorder_->CounterTotal("http_server_requests_success", BaseAttrs("Trace", "GET")), 6);
  EXPECT_EQ(recorder_->CounterTotal("http_server_requests", BaseAttrs("Trace", "GET")), 12);

  double failures = 0;
  for (const auto& entry : recorder_->Entries()) {
    if (entry.name == "http_server_requests_failure") failures += entry.value;
  }
  EXPECT_EQ(failures, 6);
}

}  // namespace
}  // namespace futility::otel
