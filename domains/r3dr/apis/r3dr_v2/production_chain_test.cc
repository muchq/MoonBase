// The serving chain wired the way main.cc wires it: route labels, health
// under an exhausted budget, and the transport-vs-model size seam. aura's
// middleware_test owns the chain's semantics.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "domains/r3dr/apis/r3dr_v2/smithy_handler.h"
#include "domains/r3dr/apis/r3dr_v2/test_support.h"
#include "moonbase/r3dr/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"

namespace r3dr_v2 {
namespace {

using futility::rate_limiter::SlidingWindowRateLimiter;
using futility::rate_limiter::SlidingWindowRateLimiterConfig;

// Mutexed like aura's: the beast transport fires on_rejected on an io
// thread, and the test thread reads while that thread is still live.
class RecordingSink final : public aura::HttpMetricsSink {
 public:
  void RecordRequestStart(const std::string& /*method*/) override {}
  void RecordRequestComplete(const std::string& route, const std::string& /*method*/,
                             int status_code, std::chrono::microseconds /*duration*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    completes_.push_back({route, status_code});
  }
  std::vector<std::pair<std::string, int>> completes() {
    const std::lock_guard<std::mutex> lock(mu_);
    return completes_;
  }

 private:
  std::mutex mu_;
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
        store_(std::make_shared<FakeUrlStore>()),
        server_(
            std::make_shared<SmithyShortenerHandler>(MakeShortener(store_, [] { return kNow; }))) {
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

  smithy::http::HttpResponse RedirectAs(const std::string& peer) {
    smithy::http::HttpRequest request;
    request.method = "GET";
    request.target = "/r3dr/v2/r/DAA";
    request.peer_address = peer;
    auto response = loopback_->Send(std::move(request));
    EXPECT_TRUE(response.ok());
    return response.ok() ? *response : smithy::http::HttpResponse{};
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  std::shared_ptr<FakeUrlStore> store_;
  moonbase::r3dr::R3drV2Server server_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

TEST_F(ProductionChainTest, ServesRedirectHealthAnd429ThroughTheChain) {
  store_->targets["DAA"] = Target{"https://www.example.com", kNow + absl::Hours(1)};

  for (int i = 0; i < kMaxRequestsPerKey; ++i) {
    EXPECT_EQ(RedirectAs("203.0.113.4").status, 302);
  }
  const auto completes = sink_->completes();
  ASSERT_FALSE(completes.empty());
  // Bounded route vocabulary: slug scans cannot mint series.
  EXPECT_EQ(completes[0], (std::pair<std::string, int>{"Redirect", 302}));

  const auto limited = RedirectAs("203.0.113.4");
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.headers.Get("retry-after").value_or(""), "60");

  // One bucket for both operations: the exhausted client can't shorten
  // either. A per-op split in main.cc must fail here.
  smithy::http::HttpRequest shorten;
  shorten.method = "POST";
  shorten.target = "/r3dr/v2/shorten";
  shorten.peer_address = "203.0.113.4";
  shorten.headers.Set("content-type", "application/json");
  shorten.body = R"({"longUrl":"https://www.example.com","expiresAt":1755003600000})";
  auto blocked = loopback_->Send(std::move(shorten));
  ASSERT_TRUE(blocked.ok());
  EXPECT_EQ(blocked->status, 429);

  // Health sits before the guard: still served for the exhausted client.
  smithy::http::HttpRequest health;
  health.method = "GET";
  health.target = "/health";
  health.peer_address = "203.0.113.4";
  auto served = loopback_->Send(std::move(health));
  ASSERT_TRUE(served.ok());
  EXPECT_EQ(served->status, 200);

  // A different client still reaches the operation.
  EXPECT_EQ(RedirectAs("203.0.113.5").status, 302);
}

TEST_F(ProductionChainTest, ShortenCarriesItsOwnRouteLabel) {
  smithy::http::HttpRequest request;
  request.method = "POST";
  request.target = "/r3dr/v2/shorten";
  request.peer_address = "203.0.113.6";
  request.headers.Set("content-type", "application/json");
  request.body = R"({"longUrl":"https://www.example.com","expiresAt":1755003600000})";
  auto response = loopback_->Send(std::move(request));
  ASSERT_TRUE(response.ok());
  EXPECT_EQ(response->status, 201);

  const auto completes = sink_->completes();
  ASSERT_FALSE(completes.empty());
  EXPECT_EQ(completes.back(), (std::pair<std::string, int>{"Shorten", 201}));
}

// The 413/400 seam: past 16KB the transport answers; inside it, generated
// validation does.
TEST_F(ProductionChainTest, TheTransportAndTheUrlBoundSplitTheOversizedSpace) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  options.max_body_bytes = std::size_t{16} * 1024;
  options.on_rejected = aura::RejectionMetrics(sink_);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  smithy::http::SocketHttpClient raw("127.0.0.1", transport.port());

  smithy::http::HttpRequest big_url;
  big_url.method = "POST";
  big_url.target = "/r3dr/v2/shorten";
  big_url.peer_address = "203.0.113.9";
  big_url.headers.Set("content-type", "application/json");
  big_url.body =
      std::string(R"({"longUrl":"https://example.com/)") + std::string(2000, 'a') + "\"}";
  const auto refused = raw.Send(big_url);
  ASSERT_TRUE(refused.ok()) << refused.error().message();
  EXPECT_EQ(refused->status, 400);

  // Past the transport limit: the handler is never invoked, and the
  // rejection lands in the instruments under the sentinel route. Whether
  // the client reads the 413 or takes the reset depends on socket
  // buffering (see one_d4_v2's twin of this test).
  const auto completes_before = sink_->completes().size();
  smithy::http::HttpRequest oversized = big_url;
  oversized.body = std::string(64 * 1024, 'x');
  const auto rejected = raw.Send(oversized);
  if (rejected.ok()) {
    EXPECT_EQ(rejected->status, 413);
  }
  const auto completes = sink_->completes();
  ASSERT_EQ(completes.size(), completes_before + 1);
  EXPECT_EQ(completes.back(), (std::pair<std::string, int>{aura::kUnmatchedRoute, 413}));

  transport.Stop();
}

}  // namespace
}  // namespace r3dr_v2
