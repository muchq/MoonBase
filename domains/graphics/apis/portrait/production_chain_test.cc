// Portrait's serving chain, integration-flavored: the aura ProductionChain
// (the same builder main.cc uses) composed around the generated Portrait
// server, driven by the generated client over loopback and once over the
// real Beast transport. Chain behavior in isolation — rate-limit keying,
// access-log shape, rejection labels — is aura's own test suite
// (//domains/platform/libs/aura:middleware_test); this file pins that
// portrait's wiring of it serves traces, health, and 429s end to end.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/graphics/apis/portrait/test_support.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/client/config.h"
#include "smithy/core/blob.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/socket_transport.h"

namespace {

using futility::rate_limiter::SlidingWindowRateLimiter;
using futility::rate_limiter::SlidingWindowRateLimiterConfig;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::PortraitHandler;
using moonbase::portrait::TraceInput;
using moonbase::portrait::TraceOutput;
using portrait::test_support::LoopbackHarness;
using portrait::test_support::ValidTraceInput;
using portrait::test_support::ValidTraceJson;

// No rendering here — the chain's interaction with the generated server is
// the subject under test.
class StubHandler final : public PortraitHandler {
 public:
  smithy::Outcome<TraceOutput> Trace(const TraceInput& input,
                                     const smithy::server::RequestContext& /*context*/) override {
    TraceOutput output;
    output.base64_png = smithy::Blob::FromString("png");
    output.width = input.output.width;
    output.height = input.output.height;
    return output;
  }
};

class RecordingSink final : public aura::HttpMetricsSink {
 public:
  void RecordRequestStart(const std::string& /*route*/, const std::string& /*method*/) override {}
  void RecordRequestComplete(const std::string& route, const std::string& /*method*/,
                             int status_code, std::chrono::microseconds /*duration*/) override {
    completes_.push_back({route, status_code});
  }
  std::vector<std::pair<std::string, int>> completes_;
};

class PortraitProductionChainTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRequestsPerKey = 3;

  PortraitProductionChainTest()
      : sink_(std::make_shared<RecordingSink>()),
        limiter_(std::make_shared<SlidingWindowRateLimiter<std::string>>(
            SlidingWindowRateLimiterConfig{.max_requests_per_key = kMaxRequestsPerKey,
                                           .window_size = std::chrono::seconds(60),
                                           .ttl = std::chrono::minutes(5),
                                           .cleanup_interval = std::chrono::seconds(30),
                                           .max_keys = 100})),
        harness_(std::make_shared<StubHandler>(), [this](smithy::http::RequestHandler inner) {
          return aura::ProductionChain(
              aura::ChainOptions{.metrics = sink_,
                                 .allow_request =
                                     [limiter = limiter_](const std::string& client) {
                                       return limiter->allow(client);
                                     },
                                 .retry_after = std::chrono::seconds(60)},
              std::move(inner));
        }) {}

  smithy::http::HttpResponse PostTraceAs(const std::string& peer) {
    smithy::http::HttpRequest request;
    request.method = "POST";
    request.target = "/portrait/v1/trace";
    request.peer_address = peer;
    request.headers.Set("content-type", "application/json");
    request.body = ValidTraceJson();
    return harness_.Send(std::move(request));
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  LoopbackHarness harness_;
};

TEST_F(PortraitProductionChainTest, ServesTraceHealthAnd429ThroughTheChain) {
  // Trace requests flow through to the generated server and are observed.
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(PostTraceAs("203.0.113.4").status, 200);
  }
  ASSERT_FALSE(sink_->completes_.empty());
  EXPECT_EQ(sink_->completes_[0], (std::pair<std::string, int>{"/portrait/v1/trace", 200}));

  // The budget is enforced per client, with Retry-After.
  const auto limited = PostTraceAs("203.0.113.4");
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");

  // Health sits before the guard: still served for the exhausted client.
  smithy::http::HttpRequest health;
  health.method = "GET";
  health.target = "/health";
  health.peer_address = "203.0.113.4";
  EXPECT_EQ(harness_.Send(std::move(health)).status, 200);
}

// One pass over the real Beast transport, shaped like main.cc: the
// generated client round-trips through a real socket, and the shrunken body
// limit rejects oversized payloads at the transport with the 413 recorded
// in the same instruments.
TEST_F(PortraitProductionChainTest, BeastTransportServesChainAndEnforcesBodyLimit) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = 2048;
  options.on_rejected = aura::RejectionMetrics(sink_);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(harness_.handler()).ok());

  smithy::ClientConfig config;
  config.endpoint = "http://127.0.0.1:" + std::to_string(transport.port());
  auto created = PortraitClient::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.error().message();
  PortraitClient client = std::move(*created);

  const auto traced = client.Trace(ValidTraceInput());
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->width, 20);

  const auto completes_before = sink_->completes_.size();
  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());
  smithy::http::HttpRequest oversized;
  oversized.method = "POST";
  oversized.target = "/portrait/v1/trace";
  oversized.headers.Set("content-type", "application/json");
  oversized.body = std::string(4096, 'x');
  const auto rejected = raw.Send(oversized);
  ASSERT_TRUE(rejected.ok()) << rejected.error().message();
  EXPECT_EQ(rejected->status, 413);
  ASSERT_EQ(sink_->completes_.size(), completes_before + 1);
  EXPECT_EQ(sink_->completes_.back(), (std::pair<std::string, int>{"/portrait/v1/trace", 413}));

  transport.Stop();
}

}  // namespace
