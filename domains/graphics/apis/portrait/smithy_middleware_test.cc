// Phase 3 middleware tests (https://github.com/muchq/MoonBase/issues/1168):
// the production middleware chain — meerkat-parity observability, health,
// per-client rate limiting keyed on the derived client address — composed
// around the generated server, driven over loopback plus one pass over the
// real Beast socket transport.

#include "domains/graphics/apis/portrait/smithy_middleware.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/client/config.h"
#include "smithy/core/blob.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/forwarded.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"
#include "smithy/server/middleware.h"

namespace {

using futility::rate_limiter::SlidingWindowRateLimiter;
using futility::rate_limiter::SlidingWindowRateLimiterConfig;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::PortraitHandler;
using moonbase::portrait::PortraitServer;
using moonbase::portrait::TraceInput;
using moonbase::portrait::TraceOutput;

constexpr char kValidTraceBody[] = R"({
  "scene": {
    "spheres": [
      {"center": [0.0, -1.0, 3.0], "radius": 1.0, "color": [255, 0, 0],
       "specular": 500.0, "reflective": 0.2}
    ]
  },
  "perspective": {"cameraPosition": [0.0, 0.0, -1.0], "cameraFocus": [0.0, 0.0, 0.0]},
  "output": {"width": 20, "height": 20}
})";

// No rendering here — middleware behavior is the subject under test.
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

struct StartCall {
  std::string route;
  std::string method;
};

struct CompleteCall {
  std::string route;
  std::string method;
  int status;
  std::chrono::microseconds duration;
};

class RecordingSink final : public portrait::HttpMetricsSink {
 public:
  void RecordRequestStart(const std::string& route, const std::string& method) override {
    const std::lock_guard<std::mutex> lock(mu_);
    starts_.push_back({route, method});
  }
  void RecordRequestComplete(const std::string& route, const std::string& method, int status_code,
                             std::chrono::microseconds duration) override {
    const std::lock_guard<std::mutex> lock(mu_);
    completes_.push_back({route, method, status_code, duration});
  }

  std::vector<StartCall> starts() {
    const std::lock_guard<std::mutex> lock(mu_);
    return starts_;
  }
  std::vector<CompleteCall> completes() {
    const std::lock_guard<std::mutex> lock(mu_);
    return completes_;
  }

 private:
  std::mutex mu_;
  std::vector<StartCall> starts_;
  std::vector<CompleteCall> completes_;
};

// The production chain shape from portrait_smithy_main.cc, with a small
// rate-limit budget so tests can exhaust it quickly. The rate limiter keys
// on the ADR-0012 derived client address anchored at peer_address, which
// Loopback lets tests stamp directly (a real transport stamps it from the
// connection).
class SmithyMiddlewareTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRequestsPerKey = 3;
  // The trusted reverse-proxy tier, standing in for compose's pinned Caddy
  // address. x-forwarded-for entries count only through this peer.
  static constexpr char kProxy[] = "10.0.0.2";

  void SetUp() override {
    sink_ = std::make_shared<RecordingSink>();
    SlidingWindowRateLimiterConfig config{.max_requests_per_key = kMaxRequestsPerKey,
                                          .window_size = std::chrono::seconds(60),
                                          .ttl = std::chrono::minutes(5),
                                          .cleanup_interval = std::chrono::seconds(30),
                                          .max_keys = 100};
    limiter_ = std::make_shared<SlidingWindowRateLimiter<std::string>>(config);
    server_ = std::make_unique<PortraitServer>(std::make_shared<StubHandler>());
    handler_ = smithy::server::Chain(
        {portrait::MeerkatParityObservability(sink_), smithy::server::HealthEndpoint("/health"),
         portrait::RateLimitByClientAddress(limiter_, smithy::http::TrustedProxies({kProxy}),
                                            std::chrono::seconds(60))},
        server_->Handler());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback_->Start(handler_).ok());
  }

  // peer is the L4 identity the client-address derivation anchors on. An
  // unnamed peer gets a fresh TEST-NET address per send, so sends that don't
  // exercise keying never share a rate-limit bucket; pass an explicit peer
  // to exercise it.
  smithy::http::HttpResponse Send(
      const std::string& method, const std::string& target, const std::string& body = "",
      const std::string& peer = "",
      const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    smithy::http::HttpRequest request;
    request.method = method;
    request.target = target;
    request.peer_address = peer.empty() ? "192.0.2." + std::to_string(++next_default_peer_) : peer;
    if (!body.empty()) {
      request.headers.Set("content-type", "application/json");
      request.body = body;
    }
    for (const auto& [name, value] : headers) {
      request.headers.Set(name, value);
    }
    auto response = loopback_->Send(request);
    if (!response.ok()) {
      ADD_FAILURE() << "loopback send failed: " << response.error().message();
      return {};
    }
    return *response;
  }

  // Sends one trace request through the chain and returns the access-log
  // line it produced.
  std::string AccessLogLineFor(const std::vector<std::pair<std::string, std::string>>& headers) {
    std::string line;
    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_, testing::HasSubstr("trace_id=")))
        .WillOnce(testing::SaveArg<2>(&line));
    log.StartCapturingLogs();
    EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, "", headers).status, 200);
    log.StopCapturingLogs();
    return line;
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  std::unique_ptr<PortraitServer> server_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
  int next_default_peer_ = 0;
};

TEST_F(SmithyMiddlewareTest, HealthServedAndObserved) {
  const auto response = Send("GET", "/health");
  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.body, R"({"status":"healthy"})");

  // Meerkat's metrics interceptor counted health probes; parity keeps that.
  const auto starts = sink_->starts();
  const auto completes = sink_->completes();
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].route, "/health");
  EXPECT_EQ(starts[0].method, "GET");
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].status, 200);
}

TEST_F(SmithyMiddlewareTest, TraceObservedWithRouteMethodAndStatus) {
  const auto response = Send("POST", "/portrait/v1/trace", kValidTraceBody);
  ASSERT_EQ(response.status, 200) << response.body;

  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, "/portrait/v1/trace");
  EXPECT_EQ(completes[0].method, "POST");
  EXPECT_EQ(completes[0].status, 200);
  EXPECT_GE(completes[0].duration.count(), 0);
}

TEST_F(SmithyMiddlewareTest, QueryStringStrippedFromRouteLabel) {
  Send("GET", "/health?probe=1");
  const auto starts = sink_->starts();
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].route, "/health");
}

TEST_F(SmithyMiddlewareTest, RateLimitsPerClientAddressWithRetryAfter) {
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, "203.0.113.4").status, 200);
  }
  const auto limited = Send("POST", "/portrait/v1/trace", kValidTraceBody, "203.0.113.4");
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");
  EXPECT_NE(limited.body.find("Too many requests"), std::string::npos);

  // A different client is still admitted.
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, "203.0.113.8").status, 200);

  // Rate-limited requests are observed (meerkat counted 429s with
  // error_type=rate_limited; the sink sees the status that drives it).
  bool saw_429 = false;
  for (const auto& complete : sink_->completes()) {
    if (complete.status == 429) saw_429 = true;
  }
  EXPECT_TRUE(saw_429);
}

// ADR-0012 keying through the production chain shape: behind the trusted
// proxy the forwarded client is the key; a direct client writing the same
// header keys as its own peer, so spoofing cannot drain another client's
// bucket (or mint fresh buckets to evade its own).
TEST_F(SmithyMiddlewareTest, SpoofedForwardedForCannotReachAnotherClientsBucket) {
  const std::vector<std::pair<std::string, std::string>> forwarded = {
      {"X-Forwarded-For", "203.0.113.9"}};
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, kProxy, forwarded).status, 200);
  }
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, kProxy, forwarded).status, 429);

  // A different client behind the same trusted proxy has its own budget:
  // the key is the forwarded client, not the proxy peer. (This is the
  // assertion that fails if the trust-set walk is dropped and all Caddy
  // traffic collapses into one bucket.)
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, kProxy,
                 {{"X-Forwarded-For", "203.0.113.50"}})
                .status,
            200);

  // Same header from an untrusted peer: client-authored noise. The request
  // keys as the peer itself and is admitted despite the exhausted bucket
  // its header names.
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, "198.51.100.7", forwarded).status,
            200);
}

TEST_F(SmithyMiddlewareTest, HealthIsNeverRateLimited) {
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    Send("POST", "/portrait/v1/trace", kValidTraceBody, "203.0.113.7");
  }
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, "203.0.113.7").status, 429);
  // Deliberate change from meerkat: probes sit before the guard in the chain.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(Send("GET", "/health", "", "203.0.113.7").status, 200);
  }
}

// The portrait-owned slice of trace identity is the access-log line's
// trace_id= field, parsed from the traceparent the transport guard mints or
// joins at ingress. The mint/join/replace mechanics themselves are
// upstream-tested (smithy-cpp ADR-0011); these tests pin what portrait logs.
std::string TraceIdIn(const std::string& log_line) {
  constexpr char kKey[] = "trace_id=";
  const auto pos = log_line.find(kKey);
  if (pos == std::string::npos) return "";
  const auto start = pos + sizeof(kKey) - 1;
  return log_line.substr(start, log_line.find(' ', start) - start);
}

bool IsLowercaseHex32(const std::string& value) {
  if (value.size() != 32) return false;
  for (const char c : value) {
    if (!(('0' <= c && c <= '9') || ('a' <= c && c <= 'f'))) return false;
  }
  return true;
}

TEST_F(SmithyMiddlewareTest, AccessLogJoinsInboundTraceIdentity) {
  constexpr char kInbound[] = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  EXPECT_EQ(TraceIdIn(AccessLogLineFor({{"traceparent", kInbound}})),
            "4bf92f3577b34da6a3ce929d0e0e4736");
}

TEST_F(SmithyMiddlewareTest, AccessLogCarriesMintedTraceIdWhenInboundIsAbsentOrMalformed) {
  const std::string minted = TraceIdIn(AccessLogLineFor({}));
  EXPECT_TRUE(IsLowercaseHex32(minted)) << "minted: " << minted;
  const std::string replaced = TraceIdIn(AccessLogLineFor({{"traceparent", "garbage"}}));
  EXPECT_TRUE(IsLowercaseHex32(replaced)) << "replaced: " << replaced;
}

// A 431 can fire before Beast parses the method or target; the adapter maps
// those to a stable label instead of empty strings dashboards would drop.
TEST(RejectionMetricsTest, UnparsedRejectionLandsOnStableLabels) {
  auto sink = std::make_shared<RecordingSink>();
  portrait::RejectionMetrics(sink)({.status = 431});
  const auto completes = sink->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, "(unparsed)");
  EXPECT_EQ(completes[0].method, "(unparsed)");
  EXPECT_EQ(completes[0].status, 431);
}

// One pass over the real Beast transport, shaped like portrait_smithy_main:
// the generated client round-trips through a real socket, and the shrunken
// body limit rejects oversized payloads at the transport.
TEST_F(SmithyMiddlewareTest, BeastTransportServesChainAndEnforcesBodyLimit) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = 2048;
  options.on_rejected = portrait::RejectionMetrics(sink_);
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  smithy::ClientConfig config;
  config.endpoint = "http://127.0.0.1:" + std::to_string(transport.port());
  auto created = PortraitClient::Create(std::move(config));
  ASSERT_TRUE(created.ok()) << created.error().message();
  PortraitClient client = std::move(*created);

  TraceInput input;
  moonbase::portrait::Sphere sphere;
  sphere.center = {0.0, -1.0, 3.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 500.0;
  sphere.reflective = 0.2;
  input.scene.spheres = {sphere};
  input.perspective.cameraPosition = {0.0, 0.0, -1.0};
  input.perspective.cameraFocus = {0.0, 0.0, 0.0};
  input.output.width = 20;
  input.output.height = 20;
  const auto traced = client.Trace(input);
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->width, 20);

  // A declared Content-Length over max_body_bytes is rejected at the
  // transport with a readable 413 (smithy-cpp 79667d2: 413 + bounded
  // lingering close). The request never reaches the middleware chain, but the
  // on_rejected hook records it in the same instruments (smithy-cpp #102).
  const auto completes_before = sink_->completes().size();
  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());
  smithy::http::HttpRequest oversized;
  oversized.method = "POST";
  oversized.target = "/portrait/v1/trace";
  oversized.headers.Set("content-type", "application/json");
  oversized.body = std::string(4096, 'x');
  const auto rejected = raw.Send(oversized);
  ASSERT_TRUE(rejected.ok()) << rejected.error().message();
  EXPECT_EQ(rejected->status, 413);
  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), completes_before + 1);
  EXPECT_EQ(completes.back().route, "/portrait/v1/trace");
  EXPECT_EQ(completes.back().status, 413);

  transport.Stop();
}

}  // namespace
