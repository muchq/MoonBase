// one_d4_v2's serving chain, integration-flavored: aura's ProductionChain
// composed around the real generated router, the way main.cc composes it.
// aura's middleware_test owns the chain's semantics; what is pinned here is
// the wiring of *this* server — the route label the router stamps, health
// surviving an exhausted budget, the limiter answering before analysis
// runs, and the transport's body limit meeting the handler's PGN cap.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/one_d4_v2/smithy_handler.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/one_d4/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"

namespace one_d4_v2 {
namespace {

using futility::rate_limiter::SlidingWindowRateLimiter;
using futility::rate_limiter::SlidingWindowRateLimiterConfig;

constexpr char kScholarsMateJson[] =
    R"({"pgn": "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0"})";

class RecordingSink final : public aura::HttpMetricsSink {
 public:
  void RecordRequestStart(const std::string& /*method*/) override {}
  void RecordRequestComplete(const std::string& route, const std::string& /*method*/,
                             int status_code, std::chrono::microseconds /*duration*/) override {
    completes_.push_back({route, status_code});
  }
  std::vector<std::pair<std::string, int>> completes_;
};

class ProductionChainTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRequestsPerKey = 3;

  ProductionChainTest()
      : sink_(std::make_shared<RecordingSink>()),
        limiter_(std::make_shared<SlidingWindowRateLimiter<std::string>>(
            SlidingWindowRateLimiterConfig{.max_requests_per_key = kMaxRequestsPerKey,
                                           .window_size = std::chrono::seconds(60),
                                           .ttl = std::chrono::minutes(5),
                                           .cleanup_interval = std::chrono::seconds(30),
                                           .max_keys = 100})),
        server_(std::make_shared<SmithyAnalyzeHandler>()) {
    handler_ = aura::ProductionChain(
        aura::ChainOptions{
            .metrics = sink_,
            .allow_request =
                [limiter = limiter_](const std::string& client) { return limiter->allow(client); },
            .retry_after = std::chrono::seconds(60)},
        server_.Handler());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    const auto started = loopback_->Start(handler_);
    EXPECT_TRUE(started.ok());
  }

  smithy::http::HttpResponse AnalyzeAs(const std::string& peer) {
    smithy::http::HttpRequest request;
    request.method = "POST";
    request.target = "/v2/analyze";
    request.peer_address = peer;
    request.headers.Set("content-type", "application/json");
    request.body = kScholarsMateJson;
    auto response = loopback_->Send(std::move(request));
    EXPECT_TRUE(response.ok());
    return response.ok() ? *response : smithy::http::HttpResponse{};
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  moonbase::one_d4::OneD4V2Server server_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

TEST_F(ProductionChainTest, ServesAnalyzeHealthAnd429ThroughTheChain) {
  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(AnalyzeAs("203.0.113.4").status, 200);
  }
  ASSERT_FALSE(sink_->completes_.empty());
  // The operation name the generated router matched — the bounded route
  // vocabulary, not the raw path.
  EXPECT_EQ(sink_->completes_[0], (std::pair<std::string, int>{"Analyze", 200}));

  const auto limited = AnalyzeAs("203.0.113.4");
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");

  // Health sits before the guard: still served for the exhausted client,
  // which is what keeps a busy service from probing as a sick one.
  smithy::http::HttpRequest health;
  health.method = "GET";
  health.target = "/health";
  health.peer_address = "203.0.113.4";
  auto served = loopback_->Send(std::move(health));
  ASSERT_TRUE(served.ok());
  EXPECT_EQ(served->status, 200);

  // A different client still reaches the operation.
  EXPECT_EQ(AnalyzeAs("203.0.113.5").status, 200);
}

// The 413/400 seam: the transport's 1MB limit is what answers a body too
// large to read, and the handler's 256KB PGN cap answers one small enough
// to read but too large to analyze. Both are deliberate; this pins which
// one a caller meets where, shaped like main.cc's options.
TEST_F(ProductionChainTest, TheTransportAndThePgnCapSplitTheOversizedSpace) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = std::size_t{1} * 1024 * 1024;
  options.on_rejected = aura::RejectionMetrics(sink_);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());

  // Over the PGN cap, under the transport limit: read fully, refused by the
  // handler as the modeled 400.
  smithy::http::HttpRequest big_pgn;
  big_pgn.method = "POST";
  big_pgn.target = "/v2/analyze";
  big_pgn.peer_address = "203.0.113.9";
  big_pgn.headers.Set("content-type", "application/json");
  big_pgn.body = std::string("{\"pgn\": \"") + std::string(300 * 1024, 'x') + "\"}";
  const auto refused = raw.Send(big_pgn);
  ASSERT_TRUE(refused.ok());
  EXPECT_EQ(refused->status, 400);

  // Over the transport limit: never reaches the handler at all.
  smithy::http::HttpRequest oversized = big_pgn;
  oversized.body = std::string(2 * 1024 * 1024, 'x');
  const auto rejected = raw.Send(oversized);
  ASSERT_TRUE(rejected.ok());
  EXPECT_EQ(rejected->status, 413);

  transport.Stop();
}

}  // namespace
}  // namespace one_d4_v2
