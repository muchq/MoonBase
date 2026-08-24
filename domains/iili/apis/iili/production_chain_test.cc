// The serving chain wired the way main.cc wires it: route labels, health
// under an exhausted budget, and the transport-vs-model size seam. aura's
// middleware_test owns the chain's semantics.

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/iili/apis/iili/smithy_handler.h"
#include "domains/iili/apis/iili/test_support.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/iili/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"

namespace iili {
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
    request.target = "/iili/v1/r/DAA";
    request.peer_address = peer;
    auto response = loopback_->Send(std::move(request));
    EXPECT_TRUE(response.ok());
    return response.ok() ? *response : smithy::http::HttpResponse{};
  }

  std::shared_ptr<RecordingSink> sink_;
  std::shared_ptr<SlidingWindowRateLimiter<std::string>> limiter_;
  std::shared_ptr<FakeUrlStore> store_;
  moonbase::iili::IiliServer server_;
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
  shorten.target = "/iili/v1/shorten";
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
  request.target = "/iili/v1/shorten";
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
  big_url.target = "/iili/v1/shorten";
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

// Raw bytes to the transport and back. SocketHttpClient would not do: it
// knows a HEAD response has no body and stops after the headers, so it
// reports an empty body whether or not the server sent one.
std::string RawRoundTrip(int port, const std::string& request_bytes) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return {};
  timeval timeout{.tv_sec = 10, .tv_usec = 0};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1 ||
      ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return {};
  }
  (void)::send(fd, request_bytes.data(), request_bytes.size(), 0);
  std::string received;
  char scratch[1024];
  for (;;) {
    const auto n = ::recv(fd, scratch, sizeof(scratch), 0);
    if (n <= 0) break;
    received.append(scratch, static_cast<std::size_t>(n));
  }
  ::close(fd);
  return received;
}

std::string BodyOf(const std::string& raw) {
  const auto end = raw.find("\r\n\r\n");
  return end == std::string::npos ? std::string() : raw.substr(end + 4);
}

std::string LowerHeadersOf(const std::string& raw) {
  const auto end = raw.find("\r\n\r\n");
  std::string headers = end == std::string::npos ? raw : raw.substr(0, end);
  for (char& c : headers) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return headers;
}

// The declared length, or "<none>" when the header is absent. Two responses
// that both declare nothing compare equal, so a test asserting HEAD's length
// against the GET's has to pin the GET to a real number first.
std::string ContentLengthOf(const std::string& raw) {
  const std::string headers = LowerHeadersOf(raw);
  const auto at = headers.find("content-length: ");
  if (at == std::string::npos) return "<none>";
  const auto start = at + std::string("content-length: ").size();
  return headers.substr(start, headers.find("\r\n", start) - start);
}

// BodyOf reports an empty body for a read that never reached the separator,
// which is indistinguishable from the "no octets" a HEAD is supposed to show.
// Every raw assertion below goes through this first so a truncated response
// fails instead of confirming the property under test.
::testing::AssertionResult IsCompleteResponse(const std::string& raw) {
  if (raw.empty()) return ::testing::AssertionFailure() << "empty read";
  if (raw.find("\r\n\r\n") == std::string::npos) {
    return ::testing::AssertionFailure() << "no header/body separator in: " << raw;
  }
  return ::testing::AssertionSuccess();
}

// The framing wire_test cannot see (#1433): a HEAD answers the length the GET
// would and none of its octets. Not Content-Length: 0 — that is what a handler
// clearing the body to "omit" it produces, and it is a claim about the link
// rather than about the request.
TEST_F(ProductionChainTest, HeadRedirectCarriesTheGetsLengthAndNoBodyOnTheWire) {
  store_->targets["DAA"] = Target{"https://www.example.com/target", kNow + absl::Hours(1)};

  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  const std::string head = RawRoundTrip(
      transport.port(), "HEAD /iili/v1/r/DAA HTTP/1.1\r\nhost: x\r\nconnection: close\r\n\r\n");
  const std::string get = RawRoundTrip(
      transport.port(), "GET /iili/v1/r/DAA HTTP/1.1\r\nhost: x\r\nconnection: close\r\n\r\n");
  ASSERT_TRUE(IsCompleteResponse(head));
  ASSERT_TRUE(IsCompleteResponse(get));

  // The GET first: the length below means nothing unless a body actually
  // ships and the header describes it. alloy conformance pins this "{}" at 3xx.
  EXPECT_EQ(BodyOf(get), "{}") << get;
  EXPECT_EQ(ContentLengthOf(get), std::to_string(BodyOf(get).size())) << get;

  EXPECT_EQ(BodyOf(head), "") << "HEAD answered with a body: " << head;
  EXPECT_EQ(ContentLengthOf(head), ContentLengthOf(get))
      << "HEAD did not report the GET's length: " << head;
  // What the unfurler came for.
  EXPECT_NE(LowerHeadersOf(head).find("location: https://www.example.com/target"),
            std::string::npos)
      << head;

  transport.Stop();
}

// The other branch of the generated server: a modeled error serializes its own
// body, and the method still decides whether the octets ship. A guard that
// only covered the success path would let the 404 carry one.
TEST_F(ProductionChainTest, HeadOnAnUnknownSlugIsFramedLikeItsGetToo) {
  smithy::http::BeastServerTransport::Options options;
  options.address = "127.0.0.1";
  options.port = 0;
  smithy::http::BeastServerTransport transport(options);
  ASSERT_TRUE(transport.Start(handler_).ok());

  const std::string head = RawRoundTrip(
      transport.port(), "HEAD /iili/v1/r/zzz HTTP/1.1\r\nhost: x\r\nconnection: close\r\n\r\n");
  const std::string get = RawRoundTrip(
      transport.port(), "GET /iili/v1/r/zzz HTTP/1.1\r\nhost: x\r\nconnection: close\r\n\r\n");
  ASSERT_TRUE(IsCompleteResponse(head));
  ASSERT_TRUE(IsCompleteResponse(get));

  EXPECT_NE(head.find("404"), std::string::npos) << head;
  EXPECT_EQ(BodyOf(get), R"({"message":"no such link"})") << get;
  EXPECT_EQ(ContentLengthOf(get), std::to_string(BodyOf(get).size())) << get;
  EXPECT_EQ(BodyOf(head), "") << "HEAD answered with a body: " << head;
  EXPECT_EQ(ContentLengthOf(head), ContentLengthOf(get))
      << "HEAD did not report the GET's length: " << head;

  transport.Stop();
}

}  // namespace
}  // namespace iili
