// Phase 3 middleware tests (https://github.com/muchq/MoonBase/issues/1168):
// the production middleware chain — meerkat-parity observability, health,
// X-Forwarded-For rate limiting — composed around the generated server,
// driven over loopback plus one pass over the real Beast socket transport.

#include "domains/graphics/apis/portrait/smithy_middleware.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/client/config.h"
#include "smithy/core/blob.h"
#include "smithy/http/beast_transport.h"
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
  smithy::Outcome<TraceOutput> Trace(const TraceInput& input) override {
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
// rate-limit budget so tests can exhaust it quickly.
class SmithyMiddlewareTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRequestsPerKey = 3;

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
         portrait::RateLimitByForwardedFor(limiter_, std::chrono::seconds(60))},
        server_->Handler());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback_->Start(handler_).ok());
  }

  smithy::http::HttpResponse Send(
      const std::string& method, const std::string& target, const std::string& body = "",
      const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    smithy::http::HttpRequest request;
    request.method = method;
    request.target = target;
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

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  std::unique_ptr<PortraitServer> server_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
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

TEST_F(SmithyMiddlewareTest, RateLimitsPerForwardedForKeyWithRetryAfter) {
  const std::vector<std::pair<std::string, std::string>> alice = {{"X-Forwarded-For", "1.2.3.4"}};
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, alice).status, 200);
  }
  const auto limited = Send("POST", "/portrait/v1/trace", kValidTraceBody, alice);
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");
  EXPECT_NE(limited.body.find("Too many requests"), std::string::npos);

  // A different client key is still admitted.
  const std::vector<std::pair<std::string, std::string>> bob = {{"X-Forwarded-For", "5.6.7.8"}};
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, bob).status, 200);

  // Rate-limited requests are observed (meerkat counted 429s with
  // error_type=rate_limited; the sink sees the status that drives it).
  bool saw_429 = false;
  for (const auto& complete : sink_->completes()) {
    if (complete.status == 429) saw_429 = true;
  }
  EXPECT_TRUE(saw_429);
}

TEST_F(SmithyMiddlewareTest, HealthIsNeverRateLimited) {
  const std::vector<std::pair<std::string, std::string>> key = {{"X-Forwarded-For", "9.9.9.9"}};
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    Send("POST", "/portrait/v1/trace", kValidTraceBody, key);
  }
  EXPECT_EQ(Send("POST", "/portrait/v1/trace", kValidTraceBody, key).status, 429);
  // Deliberate change from meerkat: probes sit before the guard in the chain.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(Send("GET", "/health", "", key).status, 200);
  }
}

TEST_F(SmithyMiddlewareTest, InboundTraceIdIsEchoed) {
  const auto response =
      Send("POST", "/portrait/v1/trace", kValidTraceBody, {{"x-trace-id", "abc123"}});
  EXPECT_EQ(response.headers.Get("x-trace-id").value_or(""), "abc123");
}

TEST_F(SmithyMiddlewareTest, MissingTraceIdIsGenerated) {
  const auto response = Send("POST", "/portrait/v1/trace", kValidTraceBody);
  const std::string trace_id = response.headers.Get("x-trace-id").value_or("");
  ASSERT_FALSE(trace_id.empty());
  // Meerkat generates a random positive long rendered as digits.
  EXPECT_EQ(trace_id.find_first_not_of("0123456789"), std::string::npos);
}

// One pass over the real Beast transport, shaped like portrait_smithy_main:
// the generated client round-trips through a real socket, and the shrunken
// body limit rejects oversized payloads at the transport.
TEST_F(SmithyMiddlewareTest, BeastTransportServesChainAndEnforcesBodyLimit) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = 2048;
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

  // An oversized body is rejected at the transport before it can reach the
  // router: depending on timing the client sees a clean 413 or an aborted
  // connection (Beast may close while the client is still sending). Either
  // way the request must never reach the middleware chain.
  const auto completes_before = sink_->completes().size();
  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());
  smithy::http::HttpRequest oversized;
  oversized.method = "POST";
  oversized.target = "/portrait/v1/trace";
  oversized.headers.Set("content-type", "application/json");
  oversized.body = std::string(4096, 'x');
  const auto rejected = raw.Send(oversized);
  if (rejected.ok()) {
    EXPECT_GE(rejected->status, 400);
  }
  EXPECT_EQ(sink_->completes().size(), completes_before);

  transport.Stop();
}

}  // namespace
