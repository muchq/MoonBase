// Beyoncé Rule for smithy-cpp (if we depend on it, we put a test on it):
// every upstream behavior the aura serving chain — and through it every
// C++ service (portrait, games_hub) — relies on, pinned as a MoonBase-side
// contract test. A pin bump that changes any of these fails HERE with a
// named contract, instead of surfacing as a production surprise or a
// grep through upstream diffs (the TrustedProxies constructor removal
// that motivated this file arrived exactly that way).
//
// Deliberately depends only on smithy-cpp's non-Beast targets, so it runs
// with no sandbox setup at all — scripts/make-git-overrides.sh unblocks the
// Beast dep closure behind a proxy that 403s GitHub source archives, but
// this file should not need anyone to have run it. The Beast transport's
// own contracts (limits, deadlines, rejection hooks) are exercised by the
// service e2e suites.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "smithy/core/error.h"
#include "smithy/core/outcome.h"
#include "smithy/http/forwarded.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/trace_context.h"
#include "smithy/http/transport.h"
#include "smithy/server/middleware.h"

namespace {

using smithy::http::DeriveClient;
using smithy::http::HttpRequest;
using smithy::http::HttpResponse;
using smithy::http::RequestHandler;
using smithy::http::TrustedProxies;

HttpRequest RequestFrom(const std::string& peer, const std::string& xff = "") {
  HttpRequest request;
  request.peer_address = peer;
  if (!xff.empty()) {
    request.headers.Set("X-Forwarded-For", xff);
  }
  return request;
}

// --- TrustedProxies / DeriveClient: the ADR-0012 trust boundary. --------
// aura::TrustedProxiesFromEnv builds on Parse; PerClientRateLimit keys its
// buckets on DeriveClient's answer. Every deployed C++ service sits
// behind the same Caddy with TRUSTED_PROXY_CIDRS pinned to these
// semantics (deploy/consolidated/compose.yaml).

TEST(TrustedProxiesContract, ParseAcceptsCidrsAndNamesTheMalformedEntry) {
  auto parsed = TrustedProxies::Parse({"172.28.0.2/32", "10.0.0.0/8"});
  ASSERT_TRUE(parsed.ok());
  EXPECT_TRUE(parsed.value().Contains("172.28.0.2"));
  EXPECT_TRUE(parsed.value().Contains("10.1.2.3"));
  EXPECT_FALSE(parsed.value().Contains("192.168.0.1"));

  auto malformed = TrustedProxies::Parse({"not-a-cidr"});
  ASSERT_FALSE(malformed.ok());
  // aura logs this message verbatim on startup refusal.
  EXPECT_NE(malformed.error().message().find("not-a-cidr"), std::string::npos);
}

TEST(TrustedProxiesContract, NoneTrustsNothingAndGarbageIsInNoNetwork) {
  const TrustedProxies none = TrustedProxies::None();
  EXPECT_FALSE(none.Contains("127.0.0.1"));

  auto proxies = TrustedProxies::Parse({"10.0.0.0/8"});
  ASSERT_TRUE(proxies.ok());
  EXPECT_FALSE(proxies.value().Contains("definitely-not-an-address"));
  // IPv4-mapped IPv6 matches as the embedded IPv4.
  EXPECT_TRUE(proxies.value().Contains("::ffff:10.1.2.3"));
}

TEST(DeriveClientContract, DirectPeerAndUntrustedHeaderIgnored) {
  const TrustedProxies none = TrustedProxies::None();

  auto direct = DeriveClient(RequestFrom("203.0.113.9"), none);
  EXPECT_EQ(direct.address, "203.0.113.9");
  EXPECT_EQ(direct.source, smithy::http::DerivedClient::Source::kDirectPeer);

  // A spoofed X-Forwarded-For from an untrusted peer must not move the
  // rate-limit key.
  auto spoofed = DeriveClient(RequestFrom("203.0.113.9", "198.51.100.7"), none);
  EXPECT_EQ(spoofed.address, "203.0.113.9");
  EXPECT_EQ(spoofed.source, smithy::http::DerivedClient::Source::kUntrustedHeaderIgnored);
}

TEST(DeriveClientContract, ForwardedWalkEndsOnClientEntryAndTrustedTier) {
  auto proxies = TrustedProxies::Parse({"172.28.0.2/32"});
  ASSERT_TRUE(proxies.ok());

  // Trusted proxy in front: the client is the rightmost untrusted hop.
  auto forwarded =
      DeriveClient(RequestFrom("172.28.0.2", "198.51.100.7, 172.28.0.2"), proxies.value());
  EXPECT_EQ(forwarded.address, "198.51.100.7");
  EXPECT_EQ(forwarded.source, smithy::http::DerivedClient::Source::kForwarded);

  // No header from inside the trust set: the walk never leaves it.
  auto tier = DeriveClient(RequestFrom("172.28.0.2"), proxies.value());
  EXPECT_EQ(tier.source, smithy::http::DerivedClient::Source::kTrustedTier);

  // Loopback/hand-driven requests have no peer at all.
  auto unknown = DeriveClient(RequestFrom(""), proxies.value());
  EXPECT_EQ(unknown.source, smithy::http::DerivedClient::Source::kUnknown);
}

// --- The middleware chain aura::ProductionChain composes. ---------------

TEST(MiddlewareContract, ChainAppliesOutermostFirst) {
  std::vector<std::string> order;
  auto tag = [&order](std::string name) -> smithy::server::Middleware {
    return [&order, name = std::move(name)](RequestHandler next) -> RequestHandler {
      return [&order, name, next = std::move(next)](const HttpRequest& request) {
        order.push_back(name);
        return next(request);
      };
    };
  };
  auto handler = smithy::server::Chain({tag("outer"), tag("inner")},
                                       [](const HttpRequest&) { return HttpResponse{}; });

  handler(HttpRequest{});
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], "outer");
  EXPECT_EQ(order[1], "inner");
}

TEST(MiddlewareContract, HealthEndpointInterceptsItsPathOnlyAndPassesOthers) {
  bool reached = false;
  auto handler = smithy::server::Chain({smithy::server::HealthEndpoint("/health")},
                                       [&reached](const HttpRequest&) {
                                         reached = true;
                                         return HttpResponse{.status = 418};
                                       });

  HttpRequest health;
  health.target = "/health";
  EXPECT_EQ(handler(health).status, 200);
  EXPECT_FALSE(reached);

  HttpRequest other;
  other.target = "/anything";
  EXPECT_EQ(handler(other).status, 418);
  EXPECT_TRUE(reached);
}

TEST(MiddlewareContract, PerClientRateLimitKeysOnDerivedClientAnd429sWithRetryAfter) {
  auto proxies = TrustedProxies::Parse({"172.28.0.2/32"});
  ASSERT_TRUE(proxies.ok());

  // Deny exactly the derived client, not the proxy: proves the limiter
  // buckets on DeriveClient's answer (what aura's 429s key on).
  auto limited = smithy::server::PerClientRateLimit(
      [](const std::string& client) { return client != "198.51.100.7"; }, proxies.value(),
      std::chrono::seconds(60));
  auto handler = smithy::server::Chain(
      {limited}, [](const HttpRequest&) { return HttpResponse{.status = 200}; });

  HttpResponse denied = handler(RequestFrom("172.28.0.2", "198.51.100.7, 172.28.0.2"));
  EXPECT_EQ(denied.status, 429);
  EXPECT_EQ(denied.headers.Get("Retry-After").value_or(""), "60");

  HttpResponse allowed = handler(RequestFrom("172.28.0.2", "198.51.100.8, 172.28.0.2"));
  EXPECT_EQ(allowed.status, 200);
}

TEST(MiddlewareContract, ObservePairsStartAndCompleteWithStatusAndDuration) {
  std::atomic<int> starts{0};
  std::atomic<int> completes{0};
  smithy::server::RequestObservation seen;

  auto observe = smithy::server::Observe(
      [&completes, &seen](const smithy::server::RequestObservation& observation) {
        completes++;
        seen = observation;
      },
      [&starts](const smithy::server::RequestStart&) { starts++; });
  auto handler = smithy::server::Chain(
      {observe}, [](const HttpRequest&) -> HttpResponse { return HttpResponse{.status = 204}; });

  HttpRequest request;
  request.method = "POST";
  request.target = "/v1/thing";
  handler(request);

  EXPECT_EQ(starts.load(), 1);
  EXPECT_EQ(completes.load(), 1);
  EXPECT_EQ(seen.method, "POST");
  EXPECT_EQ(seen.target, "/v1/thing");
  EXPECT_EQ(seen.status, 204);
  EXPECT_GE(seen.duration.count(), 0);
}

// --- Traceparent parsing the aura access log builds on. -----------------

TEST(TraceContextContract, ParsesW3CTraceparentAndRejectsGarbage) {
  auto parsed =
      smithy::http::ParseTraceparent("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->trace_id, "0af7651916cd43dd8448eb211c80319c");
  EXPECT_EQ(parsed->parent_id, "b7ad6b7169203331");
  EXPECT_TRUE(parsed->sampled);

  EXPECT_FALSE(smithy::http::ParseTraceparent("").has_value());
  EXPECT_FALSE(smithy::http::ParseTraceparent("not-a-traceparent").has_value());

  // Round trip: what we format, we parse (the transport guard mints these).
  const smithy::http::TraceContext minted = smithy::http::GenerateTraceContext();
  auto reparsed = smithy::http::ParseTraceparent(smithy::http::FormatTraceparent(minted));
  ASSERT_TRUE(reparsed.has_value());
  EXPECT_EQ(reparsed->trace_id, minted.trace_id);
}

// --- Message shapes handlers construct all over MoonBase. ---------------

TEST(MessageContract, HeadersAreCaseInsensitiveFirstValueWithSetReplaceAddAppend) {
  smithy::http::Headers headers;
  headers.Add("X-Forwarded-For", "a");
  headers.Add("x-forwarded-for", "b");

  EXPECT_EQ(headers.Get("X-FORWARDED-FOR").value_or(""), "a");
  EXPECT_EQ(headers.GetAll("x-Forwarded-For").size(), 2u);

  headers.Set("X-Forwarded-For", "c");
  EXPECT_EQ(headers.GetAll("x-forwarded-for").size(), 1u);
  EXPECT_EQ(headers.Get("x-forwarded-for").value_or(""), "c");
}

TEST(MessageContract, PartialAggregateInitializationStaysValid) {
  // Upstream documents this shape as supported and warning-free; handler
  // code across portrait and games_hub writes it constantly.
  const HttpResponse response{404, {}, "not found"};
  EXPECT_EQ(response.status, 404);
  EXPECT_EQ(response.body, "not found");
  EXPECT_TRUE(response.operation.empty());

  const HttpRequest defaults{};
  EXPECT_EQ(defaults.method, "GET");
  EXPECT_EQ(defaults.target, "/");
}

// --- Loopback: the in-memory transport every middleware test stands on. -

TEST(LoopbackContract, RoundTripsNoHandlerErrorAndContainsHandlerFailure) {
  smithy::http::Loopback loopback;

  auto unstarted = loopback.Send(HttpRequest{});
  EXPECT_FALSE(unstarted.ok());

  ASSERT_TRUE(loopback
                  .Start([](const HttpRequest& request) {
                    return HttpResponse{.status = 200, .body = "echo:" + request.body};
                  })
                  .ok());
  auto round = loopback.Send(HttpRequest{.body = "hi"});
  ASSERT_TRUE(round.ok());
  EXPECT_EQ(round.value().body, "echo:hi");

  // A throwing handler must be contained at the transport boundary
  // (ADR-0003) — the caller sees a response or an Outcome error, never an
  // escaping exception.
  loopback.Stop();
  ASSERT_TRUE(
      loopback.Start([](const HttpRequest&) -> HttpResponse { throw std::runtime_error("boom"); })
          .ok());
  auto contained = loopback.Send(HttpRequest{});
  if (contained.ok()) {
    EXPECT_GE(contained.value().status, 500);
  } else {
    EXPECT_FALSE(contained.error().message().empty());
  }
}

// --- Outcome/Error idioms used at every boundary. ------------------------

TEST(OutcomeContract, OkErrorAndMoveValueSemantics) {
  smithy::Outcome<std::string> good = std::string("value");
  ASSERT_TRUE(good.ok());
  EXPECT_EQ(std::move(good).value(), "value");

  smithy::Outcome<std::string> bad = smithy::Error::Validation("nope");
  ASSERT_FALSE(bad.ok());
  EXPECT_EQ(bad.error().message(), "nope");
}

}  // namespace
