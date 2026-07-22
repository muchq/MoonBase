// The aura chain driven over loopback with a plain stub handler — no
// generated service, so the coverage here is the chain's own behavior:
// observability (instruments + access-log line), health placement,
// per-client rate limiting on the ADR-0012 derived client address, the
// transport rejection adapter, and one pass over the real Beast transport.

#include "domains/platform/libs/aura/middleware.h"

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
#include "smithy/http/beast_transport.h"
#include "smithy/http/forwarded.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"

namespace {

using futility::rate_limiter::SlidingWindowRateLimiter;
using futility::rate_limiter::SlidingWindowRateLimiterConfig;

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

class RecordingSink final : public aura::HttpMetricsSink {
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

// The innermost handler: echoes 200 for anything, so every observed status
// other than 200 was produced by the chain itself.
smithy::http::RequestHandler EchoHandler() {
  return [](const smithy::http::HttpRequest& /*request*/) {
    smithy::http::HttpResponse response;
    response.status = 200;
    response.headers.Set("content-type", "text/plain");
    response.body = "echo";
    return response;
  };
}

// The production chain via the shared builder, with a small rate-limit
// budget so tests can exhaust it quickly. The rate limiter keys on the
// ADR-0012 derived client address anchored at peer_address, which Loopback
// lets tests stamp directly (a real transport stamps it from the
// connection).
class AuraMiddlewareTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRequestsPerKey = 3;
  // The trusted reverse-proxy tier; x-forwarded-for entries count only
  // through this peer.
  static constexpr char kProxy[] = "10.0.0.2";

  AuraMiddlewareTest() {
    sink_ = std::make_shared<RecordingSink>();
    SlidingWindowRateLimiterConfig config{.max_requests_per_key = kMaxRequestsPerKey,
                                          .window_size = std::chrono::seconds(60),
                                          .ttl = std::chrono::minutes(5),
                                          .cleanup_interval = std::chrono::seconds(30),
                                          .max_keys = 100};
    limiter_ = std::make_shared<SlidingWindowRateLimiter<std::string>>(config);
    handler_ = aura::ProductionChain(
        aura::ChainOptions{
            .metrics = sink_,
            .allow_request =
                [limiter = limiter_](const std::string& client) { return limiter->allow(client); },
            .trusted_proxies = smithy::http::TrustedProxies({kProxy}),
            .retry_after = std::chrono::seconds(60)},
        EchoHandler());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    const auto started = loopback_->Start(handler_);
    if (!started.ok()) ADD_FAILURE() << "loopback start failed: " << started.error().message();
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
      request.headers.Set("content-type", "text/plain");
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

  // Sends one request through the chain and returns the access-log line it
  // produced.
  std::string AccessLogLineFor(const std::vector<std::pair<std::string, std::string>>& headers) {
    std::string line;
    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_, testing::HasSubstr("trace_id=")))
        .WillOnce(testing::SaveArg<2>(&line));
    log.StartCapturingLogs();
    EXPECT_EQ(Send("POST", "/echo", "hello", "", headers).status, 200);
    log.StopCapturingLogs();
    return line;
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
  int next_default_peer_ = 0;
};

TEST_F(AuraMiddlewareTest, HealthServedAndObserved) {
  const auto response = Send("GET", "/health");
  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.body, R"({"status":"healthy"})");

  // Health probes count in the serving instruments.
  const auto starts = sink_->starts();
  const auto completes = sink_->completes();
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].route, "/health");
  EXPECT_EQ(starts[0].method, "GET");
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].status, 200);
}

TEST_F(AuraMiddlewareTest, RequestObservedWithRouteMethodAndStatus) {
  const auto response = Send("POST", "/echo", "hello");
  ASSERT_EQ(response.status, 200) << response.body;

  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, "/echo");
  EXPECT_EQ(completes[0].method, "POST");
  EXPECT_EQ(completes[0].status, 200);
  EXPECT_GE(completes[0].duration.count(), 0);
}

TEST_F(AuraMiddlewareTest, QueryStringStrippedFromRouteLabel) {
  Send("GET", "/health?probe=1");
  const auto starts = sink_->starts();
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].route, "/health");
}

TEST_F(AuraMiddlewareTest, RateLimitsPerClientAddressWithRetryAfter) {
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(Send("POST", "/echo", "hello", "203.0.113.4").status, 200);
  }
  const auto limited = Send("POST", "/echo", "hello", "203.0.113.4");
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");
  EXPECT_NE(limited.body.find("Too many requests"), std::string::npos);

  // A different client is still admitted.
  EXPECT_EQ(Send("POST", "/echo", "hello", "203.0.113.8").status, 200);

  // Rate-limited requests are observed (429s carry error_type=rate_limited
  // in the failure counter; the sink sees the status that drives it).
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
TEST_F(AuraMiddlewareTest, SpoofedForwardedForCannotReachAnotherClientsBucket) {
  const std::vector<std::pair<std::string, std::string>> forwarded = {
      {"X-Forwarded-For", "203.0.113.9"}};
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(Send("POST", "/echo", "hello", kProxy, forwarded).status, 200);
  }
  EXPECT_EQ(Send("POST", "/echo", "hello", kProxy, forwarded).status, 429);

  // A different client behind the same trusted proxy has its own budget:
  // the key is the forwarded client, not the proxy peer. (This is the
  // assertion that fails if the trust-set walk is dropped and all proxied
  // traffic collapses into one bucket.)
  EXPECT_EQ(Send("POST", "/echo", "hello", kProxy, {{"X-Forwarded-For", "203.0.113.50"}}).status,
            200);

  // Same header from an untrusted peer: client-authored noise. The request
  // keys as the peer itself and is admitted despite the exhausted bucket
  // its header names.
  EXPECT_EQ(Send("POST", "/echo", "hello", "198.51.100.7", forwarded).status, 200);
}

TEST_F(AuraMiddlewareTest, HealthIsNeverRateLimited) {
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    Send("POST", "/echo", "hello", "203.0.113.7");
  }
  EXPECT_EQ(Send("POST", "/echo", "hello", "203.0.113.7").status, 429);
  // Probes sit before the guard in the chain, deliberately.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(Send("GET", "/health", "", "203.0.113.7").status, 200);
  }
}

// Services without a limiter (e.g. golf_hub) leave allow_request unset; the
// guard is skipped entirely and nothing ever 429s.
TEST(AuraChainWithoutLimiterTest, NoGuardWhenAllowRequestUnset) {
  auto sink = std::make_shared<RecordingSink>();
  auto handler = aura::ProductionChain(aura::ChainOptions{.metrics = sink}, EchoHandler());
  auto loopback = std::make_shared<smithy::http::Loopback>();
  ASSERT_TRUE(loopback->Start(handler).ok());

  for (int i = 0; i < 20; ++i) {
    smithy::http::HttpRequest request;
    request.method = "POST";
    request.target = "/echo";
    request.peer_address = "203.0.113.4";
    request.body = "hello";
    const auto response = loopback->Send(request);
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(response->status, 200);
  }
  EXPECT_EQ(sink->completes().size(), 20u);
}

// The access-log line's trace_id= field carries the W3C trace id parsed
// from the traceparent the transport guard mints or joins at ingress. The
// mint/join/replace mechanics themselves are upstream-tested (smithy-cpp
// ADR-0011); these tests pin what aura logs.
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

TEST_F(AuraMiddlewareTest, AccessLogJoinsInboundTraceIdentity) {
  constexpr char kInbound[] = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  EXPECT_EQ(TraceIdIn(AccessLogLineFor({{"traceparent", kInbound}})),
            "4bf92f3577b34da6a3ce929d0e0e4736");
}

TEST_F(AuraMiddlewareTest, AccessLogCarriesMintedTraceIdWhenInboundIsAbsentOrMalformed) {
  const std::string minted = TraceIdIn(AccessLogLineFor({}));
  EXPECT_TRUE(IsLowercaseHex32(minted)) << "minted: " << minted;
  const std::string replaced = TraceIdIn(AccessLogLineFor({{"traceparent", "garbage"}}));
  EXPECT_TRUE(IsLowercaseHex32(replaced)) << "replaced: " << replaced;
}

// Pin the WARNING line an operator greps for during an incident.
// Transport-to-hook delivery is upstream-tested; the mapping is aura's.
TEST(ConnectionEventLogTest, LogsKindPeerDetailAndElapsed) {
  smithy::http::BeastServerTransport::ConnectionEvent event;
  event.kind = smithy::http::BeastServerTransport::ConnectionEvent::Kind::kFramingError;
  event.peer_address = "203.0.113.9:4711";
  event.detail = "bad method";
  event.elapsed = std::chrono::milliseconds(250);

  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, testing::_,
                       "connection_event kind=framing_error peer=203.0.113.9:4711 "
                       "detail=bad method elapsed_ms=250"));
  log.StartCapturingLogs();
  aura::ConnectionEventLog()(event);
  log.StopCapturingLogs();
}

// A 431 can fire before Beast parses the method or target; the adapter maps
// those to a stable label instead of empty strings dashboards would drop.
TEST(RejectionMetricsTest, UnparsedRejectionLandsOnStableLabels) {
  auto sink = std::make_shared<RecordingSink>();
  aura::RejectionMetrics(sink)({.status = 431});
  const auto completes = sink->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, "(unparsed)");
  EXPECT_EQ(completes[0].method, "(unparsed)");
  EXPECT_EQ(completes[0].status, 431);
}

// One pass over the real Beast transport: the chain serves through a real
// socket, and a declared Content-Length over max_body_bytes is rejected at
// the transport with a 413 the on_rejected hook records in the same
// instruments (smithy-cpp #102).
TEST_F(AuraMiddlewareTest, BeastTransportServesChainAndEnforcesBodyLimit) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = 2048;
  options.on_rejected = aura::RejectionMetrics(sink_);
  // Production-shaped options; no event can fire in this test (the 413 is
  // on_rejected-only by design).
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());
  smithy::http::HttpRequest request;
  request.method = "POST";
  request.target = "/echo";
  request.headers.Set("content-type", "text/plain");
  request.body = "hello";
  const auto served = raw.Send(request);
  ASSERT_TRUE(served.ok()) << served.error().message();
  EXPECT_EQ(served->status, 200);
  EXPECT_EQ(served->body, "echo");

  const auto completes_before = sink_->completes().size();
  smithy::http::HttpRequest oversized;
  oversized.method = "POST";
  oversized.target = "/echo";
  oversized.headers.Set("content-type", "text/plain");
  oversized.body = std::string(4096, 'x');
  const auto rejected = raw.Send(oversized);
  ASSERT_TRUE(rejected.ok()) << rejected.error().message();
  EXPECT_EQ(rejected->status, 413);
  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), completes_before + 1);
  EXPECT_EQ(completes.back().route, "/echo");
  EXPECT_EQ(completes.back().status, 413);

  transport.Stop();
}

}  // namespace
