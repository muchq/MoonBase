#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WIRE_TEST_FIXTURE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WIRE_TEST_FIXTURE_H

// The raw-wire harness the Beyoncé Rule consumer-tier suites share
// (golf_wire_test, thoughts_wire_test): GamesHubHandler behind the generated
// GamesHubServer, unary requests through Loopback, streams through
// StreamRouter()->ServeSession() over an InMemoryWebSocketPair whose near
// end the test holds and drives with hand-built frames. No generated
// client — the point of these suites is that a regeneration renaming what
// the browser reads fails here even though the typed suites still pass.
//
// Deliberately non-Beast so it runs with no sandbox setup at all
// (scripts/make-git-overrides.sh unblocks Beast behind a blocking proxy,
// but these suites should not need anyone to have run it).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/games_hub_handler.h"
#include "domains/games/apis/games_hub/golf_hub.h"
#include "domains/games/apis/games_hub/id_generator.h"
#include "domains/games/apis/games_hub/rate_limiter.h"
#include "domains/games/apis/games_hub/thoughts_hub.h"
#include "domains/games/apis/games_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "moonbase/games/server.h"
#include "smithy/core/blob.h"
#include "smithy/eventstream/frame.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

namespace games_hub {

// The session route every web client mints on; renaming it strands every
// deployed client, so the string lives here verbatim. The stream routes
// are each suite's own pin.
inline constexpr char kSessionPath[] = "/games/v2/session";

inline constexpr std::chrono::milliseconds kWireReceiveBudget{5000};

// Effectively-unlimited stream budgets (#1240): these suites pin wire
// shapes, not the limiter. The same numbers as stream_test_fixture.h's
// helper, spelled here so this header stays free of the generated client.
inline RateLimits WireRateLimits() {
  RateLimits limits;
  limits.command_burst = 1e9;
  limits.command_refill_per_sec = 1e9;
  limits.chat_burst = 1e9;
  limits.chat_refill_per_sec = 1e9;
  return limits;
}

// The emit→declare sweep every suite runs (#1327), inlined rather than taken
// from stream_test_fixture.h: that header pulls the generated client, and
// running without it is this harness's whole point. These suites matter to
// the sweep precisely because they feed the handler raw frames — the one
// path where a decode could hand a hub a command the model-pinned roster
// does not name (#1323).
inline void ExpectOnlyDeclaredCounterSeriesOnTheWire(
    const futility::otel::CapturingMetricsRecorder& recorder) {
  const auto& declared = GamesHubHandler::DeclaredCounterSeries();
  for (const auto& entry : recorder.Entries()) {
    if (entry.name == "golf_sessions_active" || entry.name == "thoughts_sessions_active") {
      continue;  // the gauges
    }
    const bool found = std::any_of(declared.begin(), declared.end(), [&](const auto& series) {
      return series.name == entry.name && series.attributes == entry.attributes;
    });
    EXPECT_TRUE(found) << entry.name
                       << " is emitted but not declared, so it loses its first event (#1323)";
  }
}

// A command frame exactly as a browser client mints it (the envelope
// convention in smithy/eventstream/envelope.h): :message-type "event",
// :event-type naming the commands-union member, :content-type
// "application/json", payload the member's JSON structure.
inline smithy::eventstream::Message CommandFrame(const std::string& event_type,
                                                 const std::string& payload_json) {
  smithy::eventstream::Message frame;
  frame.headers = {{":message-type", std::string("event")},
                   {":event-type", event_type},
                   {":content-type", std::string("application/json")}};
  frame.payload = smithy::Blob::FromString(payload_json);
  return frame;
}

inline std::string HeaderText(const smithy::eventstream::Message& message, std::string_view name) {
  const std::string* value = message.FindString(name);
  return value != nullptr ? *value : "<missing>";
}

// Asserts the event envelope trio on a received frame and hands back the
// payload bytes for the golden comparison; "<no frame>" (with the failure
// already recorded by NextFrame) when nothing arrived.
inline std::string EventPayload(const std::optional<smithy::eventstream::Message>& frame,
                                const std::string& event_type) {
  if (!frame.has_value()) return "<no frame>";
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*frame, ":event-type"), event_type);
  EXPECT_EQ(HeaderText(*frame, ":content-type"), "application/json");
  return frame->payload.ToString();
}

// The sorted key set of one JSON object — the shape pin where values are
// not deterministic enough to freeze outright.
inline std::set<std::string> KeysOf(const nlohmann::json& object) {
  std::set<std::string> keys;
  for (const auto& [key, value] : object.items()) keys.insert(key);
  return keys;
}

class HubWireFixture : public ::testing::Test {
 protected:
  using Message = smithy::eventstream::Message;

  void SetUp() override {
    // The hub_e2e recipe: sequential ids make the goldens deterministic
    // (player-1, room-1); null stores select the production memory
    // implementations.
    auto vault = std::make_shared<InMemoryTicketVault>(/*ticket_ttl=*/std::chrono::seconds(60),
                                                       /*resume_ttl=*/std::chrono::seconds(60));
    auto ids = std::make_shared<SequentialIdGenerator>();
    golf_ = std::make_shared<GolfHub>(vault, std::make_shared<cards::NoShuffleDealer>(), ids,
                                      /*grace_period=*/std::chrono::seconds(60), metrics_,
                                      /*store=*/nullptr, /*chat_store=*/nullptr, WireRateLimits());
    ASSERT_TRUE(golf_->RestoreFromStore().ok());
    handler_ = std::make_shared<GamesHubHandler>(vault, ids, golf_,
                                                 std::make_shared<ThoughtsHub>(vault, metrics_));
    server_ = std::make_unique<moonbase::games::GamesHubServer>(handler_);
    ASSERT_TRUE(loopback_->Start(server_->Handler()).ok());
  }

  void TearDown() override {
    // Idempotent; unblocks any session a failed test body left parked.
    for (auto& session : sessions_) session->Close();
    // After the closes, so the disconnect counters are in the capture.
    ExpectOnlyDeclaredCounterSeriesOnTheWire(*metrics_);
  }

  // A raw POST /games/v2/session, exactly the fetch() a web client makes.
  smithy::http::HttpResponse PostSession(const std::string& body) {
    smithy::http::HttpRequest request;
    request.method = "POST";
    request.target = kSessionPath;
    request.headers.Set("content-type", "application/json");
    request.body = body;
    auto response = loopback_->Send(request);
    EXPECT_TRUE(response.ok()) << response.error().message();
    if (!response.ok()) return smithy::http::HttpResponse{};
    return *std::move(response);
  }

  // A raw upgrade on `path` through the generated session router; the
  // caller holds the near (client) end and speaks frames itself.
  std::shared_ptr<smithy::http::WebSocket> DialStream(const std::string& path,
                                                      const std::string& query) {
    auto [near, far] = smithy::http::InMemoryWebSocketPair::Create();
    smithy::http::HttpRequest upgrade;
    upgrade.method = "GET";
    upgrade.target = path + query;
    sessions_.push_back(far);
    server_->StreamRouter()->ServeSession()(upgrade, far);
    return near;
  }

  // One frame under the budget; fails the test (and returns nullopt) on a
  // timeout, an error, or a close where a frame was expected.
  static std::optional<Message> NextFrame(smithy::http::WebSocket& socket) {
    auto received = socket.Receive(kWireReceiveBudget);
    if (!received.ok()) {
      ADD_FAILURE() << "receive failed: " << received.error().message();
      return std::nullopt;
    }
    if (!received->has_value()) {
      ADD_FAILURE() << "stream closed where a frame was expected";
      return std::nullopt;
    }
    return **received;
  }

  // Mints a session over the raw wire and returns the parsed body.
  nlohmann::json MintSession() {
    const auto response = PostSession("{}");
    EXPECT_EQ(response.status, 200) << response.body;
    return nlohmann::json::parse(response.body);
  }

  // The multi-frame tests' preamble: mint, dial `path`, consume the fresh
  // sessionReady. The session body lands in `session` so the caller keeps
  // the resumeToken.
  std::shared_ptr<smithy::http::WebSocket> DialReady(const std::string& path,
                                                     nlohmann::json& session) {
    session = MintSession();
    auto socket = DialStream(path, "?ticket=" + session["ticket"].get<std::string>());
    const auto ready = NextFrame(*socket);
    EXPECT_TRUE(ready.has_value());
    if (ready.has_value()) EXPECT_EQ(HeaderText(*ready, ":event-type"), "sessionReady");
    return socket;
  }

  std::shared_ptr<futility::otel::CapturingMetricsRecorder> metrics_ =
      std::make_shared<futility::otel::CapturingMetricsRecorder>("games_hub_test");
  std::shared_ptr<GolfHub> golf_;
  std::shared_ptr<GamesHubHandler> handler_;
  std::unique_ptr<moonbase::games::GamesHubServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_ = std::make_shared<smithy::http::Loopback>();
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions_;
};

}  // namespace games_hub

#endif
