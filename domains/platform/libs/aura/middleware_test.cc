// The aura chain driven over loopback with a plain stub handler — no
// generated service, so the coverage here is the chain's own behavior:
// observability (instruments + access-log line), health placement,
// per-client rate limiting on the ADR-0012 derived client address, the
// transport rejection adapter, and one pass over the real Beast transport.

#include "domains/platform/libs/aura/middleware.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
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
  void RecordRequestStart(const std::string& method) override {
    const std::lock_guard<std::mutex> lock(mu_);
    starts_.push_back({method});
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
// other than 200 was produced by the chain itself. Annotates its responses
// with an operation the way the generated router does, so completions carry
// the matched-handler route label; tests for the unrouted shapes use
// UnroutedHandler below.
smithy::http::RequestHandler EchoHandler() {
  return [](const smithy::http::HttpRequest& /*request*/) {
    smithy::http::HttpResponse response;
    response.status = 200;
    response.headers.Set("content-type", "text/plain");
    response.body = "echo";
    response.operation = "Echo";
    return response;
  };
}

// A handler that never annotates an operation — the shape of a generated
// router's dispatch failures (404/405/400), where no handler matched.
smithy::http::RequestHandler UnroutedHandler(int status) {
  return [status](const smithy::http::HttpRequest& /*request*/) {
    smithy::http::HttpResponse response;
    response.status = status;
    response.headers.Set("content-type", "text/plain");
    response.body = "no route";
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
            .trusted_proxies = smithy::http::TrustedProxies::Parse({kProxy}).value(),
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
    return AccessLogLineForRequest("POST", "/echo", "hello", headers, 200);
  }

  std::string AccessLogLineForRequest(
      const std::string& method, const std::string& target, const std::string& body,
      const std::vector<std::pair<std::string, std::string>>& headers, int expected_status) {
    std::string line;
    absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_, testing::HasSubstr("\"trace_id\":")))
        .WillOnce(testing::SaveArg<2>(&line));
    log.StartCapturingLogs();
    EXPECT_EQ(Send(method, target, body, "", headers).status, expected_status);
    log.StopCapturingLogs();
    return line;
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
  int next_default_peer_ = 0;
};

TEST_F(AuraMiddlewareTest, HealthServedAndObservedUnderTheProbeFilterLiteral) {
  const auto response = Send("GET", "/health");
  EXPECT_EQ(response.status, 200);
  EXPECT_EQ(response.body, R"({"status":"healthy"})");

  // Health probes count in the serving instruments, under exactly the
  // route literal prom_proxy's probeFilter subtracts and its Probes tiles
  // select (#1303, #1307). The endpoint is middleware, not a routed
  // operation, so this mapping is aura's own — nothing upstream stamps it.
  const auto starts = sink_->starts();
  const auto completes = sink_->completes();
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].method, "GET");
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, aura::kHealthRoute);
  EXPECT_EQ(completes[0].status, 200);
}

TEST_F(AuraMiddlewareTest, CompletionCarriesTheMatchedOperationAsItsRoute) {
  const auto response = Send("POST", "/echo", "hello");
  ASSERT_EQ(response.status, 200) << response.body;

  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), 1u);
  // The operation the handler annotated, not the request path: the path is
  // client-controlled and unbounded, the operation set is fixed (#1305).
  EXPECT_EQ(completes[0].route, "Echo");
  EXPECT_EQ(completes[0].method, "POST");
  EXPECT_EQ(completes[0].status, 200);
  EXPECT_GE(completes[0].duration.count(), 0);
}

TEST_F(AuraMiddlewareTest, QueryStringDoesNotDefeatTheHealthRouteMapping) {
  Send("GET", "/health?probe=1");
  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, aura::kHealthRoute);
}

// The #1305 pin: distinct unrouted paths share one sentinel label. Before
// this, every path a scanner tried became its own Prometheus series.
TEST(AuraUnroutedTest, ScannerPathsCollapseIntoTheSentinel) {
  auto sink = std::make_shared<RecordingSink>();
  auto handler = aura::ProductionChain(aura::ChainOptions{.metrics = sink}, UnroutedHandler(404));
  auto loopback = std::make_shared<smithy::http::Loopback>();
  ASSERT_TRUE(loopback->Start(handler).ok());

  for (const std::string target : {"/wp-login.php", "/admin/config", "/v1/nope?x=1"}) {
    smithy::http::HttpRequest request;
    request.method = "GET";
    request.target = target;
    request.peer_address = "203.0.113.4";
    ASSERT_TRUE(loopback->Send(request).ok());
  }

  const auto completes = sink->completes();
  ASSERT_EQ(completes.size(), 3u);
  for (const auto& complete : completes) {
    EXPECT_EQ(complete.route, aura::kUnmatchedRoute);
    EXPECT_EQ(complete.status, 404);
  }
}

// The same rule, one label over: Beast passes any wire token through as the
// method, so invented verbs collapse to CUSTOM instead of minting a series
// (and a gauge entry) per token. Lowercase "get" is deliberately in the set —
// methods are case-sensitive, so it is an invented token, not GET. The nine
// real methods pass through verbatim; the suite's GET/POST assertions
// elsewhere pin that side.
TEST(AuraUnroutedTest, InventedMethodsCollapseIntoCustom) {
  auto sink = std::make_shared<RecordingSink>();
  auto handler = aura::ProductionChain(aura::ChainOptions{.metrics = sink}, UnroutedHandler(405));
  auto loopback = std::make_shared<smithy::http::Loopback>();
  ASSERT_TRUE(loopback->Start(handler).ok());

  for (const std::string method : {"FOOBAR1", "FOOBAR2", "get"}) {
    smithy::http::HttpRequest request;
    request.method = method;
    request.target = "/echo";
    request.peer_address = "203.0.113.4";
    ASSERT_TRUE(loopback->Send(request).ok());
  }

  const auto starts = sink->starts();
  const auto completes = sink->completes();
  ASSERT_EQ(starts.size(), 3u);
  ASSERT_EQ(completes.size(), 3u);
  for (const auto& start : starts) {
    EXPECT_EQ(start.method, "CUSTOM");
  }
  for (const auto& complete : completes) {
    EXPECT_EQ(complete.method, "CUSTOM");
  }
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

// The access-log line's "trace_id" field carries the W3C trace id parsed
// from the traceparent the transport guard mints or joins at ingress. The
// mint/join/replace mechanics themselves are upstream-tested (smithy-cpp
// ADR-0011); these tests pin what aura logs.
std::string TraceIdIn(const std::string& log_line) {
  constexpr char kKey[] = "\"trace_id\":\"";
  const auto pos = log_line.find(kKey);
  if (pos == std::string::npos) return "";
  const auto start = pos + sizeof(kKey) - 1;
  return log_line.substr(start, log_line.find('"', start) - start);
}

bool IsLowercaseHex32(const std::string& value) {
  if (value.size() != 32) return false;
  for (const char c : value) {
    if (!(('0' <= c && c <= '9') || ('a' <= c && c <= 'f'))) return false;
  }
  return true;
}

// One JSON object per request, speaking the metrics vocabulary
// (http_method, route, status — #1459/#1365): a reader who sees a spike on
// a route="Echo" panel pastes the same word into a log query. The route is
// the bounded label, never the raw path; the raw path rides separately in
// "target".
TEST_F(AuraMiddlewareTest, AccessLogIsOneJsonObjectInTheMetricsVocabulary) {
  const std::string line = AccessLogLineFor({{"X-Forwarded-For", "203.0.113.9"}});

  EXPECT_TRUE(line.front() == '{' && line.back() == '}') << line;
  for (const char* field : {
           R"("log":"access")",
           R"("service_name":")",
           R"("http_method":"POST")",
           R"("route":"Echo")",
           R"("target":"/echo")",
           R"("status":200)",
           R"("duration_us":)",
           R"("response_bytes":4)",
           R"("trace_id":")",
           R"("x_forwarded_for":"203.0.113.9")",
       }) {
    EXPECT_THAT(line, testing::HasSubstr(field));
  }
}

// Sends one request through a fresh chain over the given handler and
// returns the access-log line. The fixture's chain echoes 200 with an
// operation for every path, so the unrouted shapes need their own handler.
std::string AccessLogLineThrough(smithy::http::RequestHandler handler, const std::string& target,
                                 int expected_status) {
  auto chain = aura::ProductionChain(
      aura::ChainOptions{.metrics = std::make_shared<RecordingSink>()}, std::move(handler));
  auto loopback = std::make_shared<smithy::http::Loopback>();
  EXPECT_TRUE(loopback->Start(chain).ok());

  smithy::http::HttpRequest request;
  request.method = "POST";
  request.target = target;
  request.peer_address = "192.0.2.200";

  std::string line;
  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, testing::_, testing::HasSubstr("\"trace_id\":")))
      .WillOnce(testing::SaveArg<2>(&line));
  log.StartCapturingLogs();
  const auto response = loopback->Send(request);
  EXPECT_TRUE(response.ok());
  if (response.ok()) EXPECT_EQ(response->status, expected_status);
  log.StopCapturingLogs();
  return line;
}

// The route field is the same bounded vocabulary the metrics speak: an
// unrouted request logs the sentinel, not its path — the path is in
// "target", where an unbounded value is a field, not a label.
TEST(AccessLogJsonTest, RouteFallsBackToTheSharedSentinel) {
  const std::string line = AccessLogLineThrough(UnroutedHandler(404), "/no/such/path", 404);
  EXPECT_THAT(line, testing::HasSubstr(R"("route":"unmatched")"));
  EXPECT_THAT(line, testing::HasSubstr(R"("target":"/no/such/path")"));
}

// The target is attacker-controlled and reaches the line verbatim, so JSON
// escaping is security-adjacent (smithy-cpp #203): an unescaped quote or a
// raw control byte terminates the record early and lets the rest of the URI
// masquerade as its own log entry.
TEST(AccessLogJsonTest, QuotesAndControlBytesInTheTargetAreEscaped) {
  const std::string line = AccessLogLineThrough(UnroutedHandler(404), "/e\"cho\x01?a=\\b", 404);

  EXPECT_THAT(line, testing::HasSubstr(R"("target":"/e\"cho\u0001?a=\\b")"));
  EXPECT_THAT(line, testing::Not(testing::HasSubstr("\x01")));
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

// Failed WebSocket upgrades (smithy ConnectionEvent::kUpgradeFailure) must
// name themselves — not fall through to unknown(N) after a pin bump.
TEST(ConnectionEventLogTest, LogsUpgradeFailureKind) {
  smithy::http::BeastServerTransport::ConnectionEvent event;
  event.kind = smithy::http::BeastServerTransport::ConnectionEvent::Kind::kUpgradeFailure;
  event.peer_address = "203.0.113.10:80";
  event.detail = "handshake failed";
  event.elapsed = std::chrono::milliseconds(12);

  absl::ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
  EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, testing::_,
                       "connection_event kind=upgrade_failure peer=203.0.113.10:80 "
                       "detail=handshake failed elapsed_ms=12"));
  log.StartCapturingLogs();
  aura::ConnectionEventLog()(event);
  log.StopCapturingLogs();
}

// A 431 can fire before Beast parses the method or target; the adapter maps
// the method to a stable label instead of an empty string dashboards would
// drop. The route is the sentinel like every unrouted request — a rejection
// never reached the router, and a 413 flood against distinct paths must not
// mint a series per path (#1305).
TEST(RejectionMetricsTest, UnparsedRejectionLandsOnStableLabels) {
  auto sink = std::make_shared<RecordingSink>();
  aura::RejectionMetrics(sink)({.status = 431, .peer_address = "", .method = "", .target = ""});
  const auto completes = sink->completes();
  ASSERT_EQ(completes.size(), 1u);
  EXPECT_EQ(completes[0].route, aura::kUnmatchedRoute);
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
  // Rejected before routing, so the sentinel — not the target path (#1305).
  EXPECT_EQ(completes.back().route, aura::kUnmatchedRoute);
  EXPECT_EQ(completes.back().status, 413);

  transport.Stop();
}

// The env contract every service main relies on: unset serves
// direct-connect, a parseable list is accepted, and set-but-empty or
// malformed is a refusal (nullopt) rather than a silent None().
TEST(TrustedProxiesFromEnvTest, UnsetEmptyValidAndMalformed) {
  unsetenv("TRUSTED_PROXY_CIDRS");
  EXPECT_TRUE(aura::TrustedProxiesFromEnv().has_value());

  setenv("TRUSTED_PROXY_CIDRS", " , ", 1);
  EXPECT_FALSE(aura::TrustedProxiesFromEnv().has_value());

  setenv("TRUSTED_PROXY_CIDRS", "172.28.0.2,10.0.0.0/8", 1);
  EXPECT_TRUE(aura::TrustedProxiesFromEnv().has_value());

  setenv("TRUSTED_PROXY_CIDRS", "not-a-cidr", 1);
  EXPECT_FALSE(aura::TrustedProxiesFromEnv().has_value());

  unsetenv("TRUSTED_PROXY_CIDRS");
}

}  // namespace
