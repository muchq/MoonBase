// Beyoncé Rule wire-contract tests: golden fixtures pinning the raw HTTP
// bytes and eventstream frames the golf WEB CLIENT (the browser client on
// ADR-0018's JSON-text wire) depends on. The typed-client e2e suites
// cannot catch a wire rename — both sides regenerate together — so every
// assertion here is on raw strings: paths, status codes, JSON key names,
// envelope headers (:message-type / :event-type / :exception-type /
// :content-type), and exact payload bytes (smithy::json::Encode is
// compact with sorted keys, so payloads are deterministic).
//
// The harness is hub_e2e's, minus the generated client: HubHandler behind
// the generated GolfHubServer, unary requests through Loopback, streams
// through StreamRouter()->ServeSession() over an InMemoryWebSocketPair
// whose near end this test holds and drives with hand-built frames.
//
// Deliberately non-Beast so it runs in restricted-egress sandboxes.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/hub_handler.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/rate_limiter.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "moonbase/golf/server.h"
#include "smithy/core/blob.h"
#include "smithy/eventstream/frame.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

namespace golf_hub {
namespace {

using json = nlohmann::json;
using smithy::eventstream::Message;

// The two consumer-facing routes; renaming either strands every deployed
// web client, so the strings live here verbatim.
constexpr char kSessionPath[] = "/games/v2/session";
constexpr char kPlayPath[] = "/games/v2/golf/play";

constexpr std::chrono::milliseconds kReceiveBudget{5000};

// Effectively-unlimited stream budgets (#1240): this suite pins wire
// shapes, not the limiter.
RateLimits UnlimitedRateLimits() {
  RateLimits limits;
  limits.command_burst = 1e9;
  limits.command_refill_per_sec = 1e9;
  limits.chat_burst = 1e9;
  limits.chat_refill_per_sec = 1e9;
  return limits;
}

// A command frame exactly as the browser client mints it (the envelope
// convention in smithy/eventstream/envelope.h): :message-type "event",
// :event-type naming the GolfCommands member, :content-type
// "application/json", payload the member's JSON structure.
Message CommandFrame(const std::string& event_type, const std::string& payload_json) {
  Message frame;
  frame.headers = {{":message-type", std::string("event")},
                   {":event-type", event_type},
                   {":content-type", std::string("application/json")}};
  frame.payload = smithy::Blob::FromString(payload_json);
  return frame;
}

std::string HeaderText(const Message& message, std::string_view name) {
  const std::string* value = message.FindString(name);
  return value != nullptr ? *value : "<missing>";
}

class GolfHubWireTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // The hub_e2e recipe: sequential ids make the goldens deterministic
    // (player-1, room-1); null stores select the production memory
    // implementations.
    handler_ = std::make_shared<HubHandler>(
        std::make_shared<InMemoryTicketVault>(/*ticket_ttl=*/std::chrono::seconds(60),
                                              /*resume_ttl=*/std::chrono::seconds(60)),
        std::make_shared<cards::NoShuffleDealer>(), std::make_shared<SequentialIdGenerator>(),
        /*grace_period=*/std::chrono::seconds(60),
        std::make_shared<futility::otel::MetricsRecorder>("golf_hub_test"),
        /*store=*/nullptr, /*chat_store=*/nullptr, UnlimitedRateLimits());
    ASSERT_TRUE(handler_->RestoreFromStore().ok());
    server_ = std::make_unique<moonbase::golf::GolfHubServer>(handler_);
    ASSERT_TRUE(loopback_->Start(server_->Handler()).ok());
  }

  void TearDown() override {
    // Idempotent; unblocks any session a failed test body left parked.
    for (auto& session : sessions_) session->Close();
  }

  // A raw POST /games/v2/session, exactly the fetch() the web client makes.
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

  // A raw Play upgrade through the generated session router; the caller
  // holds the near (client) end and speaks frames itself.
  std::shared_ptr<smithy::http::WebSocket> DialPlay(const std::string& query) {
    auto [near, far] = smithy::http::InMemoryWebSocketPair::Create();
    smithy::http::HttpRequest upgrade;
    upgrade.method = "GET";
    upgrade.target = std::string(kPlayPath) + query;
    sessions_.push_back(far);
    server_->StreamRouter()->ServeSession()(upgrade, far);
    return near;
  }

  // One frame under the budget; fails the test (and returns nullopt) on a
  // timeout, an error, or a close where a frame was expected.
  static std::optional<Message> NextFrame(smithy::http::WebSocket& socket) {
    auto received = socket.Receive(kReceiveBudget);
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
  json MintSession() {
    const auto response = PostSession("{}");
    EXPECT_EQ(response.status, 200) << response.body;
    return json::parse(response.body);
  }

  std::shared_ptr<HubHandler> handler_;
  std::unique_ptr<moonbase::golf::GolfHubServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_ = std::make_shared<smithy::http::Loopback>();
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions_;
};

// Consumer: the golf web client's session mint. It reads exactly these
// three camelCase keys from the response body; the byte-level prefix pins
// key order and spelling so a codegen change that renames or re-nests any
// of them fails here, not in a browser.
TEST_F(GolfHubWireTest, SessionMintPinsRawResponseBody) {
  const auto response = PostSession("{}");
  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_EQ(response.headers.Get("content-type").value_or(""), "application/json");

  // Compact JSON, sorted keys: playerId, resumeToken, ticket. The token
  // and ticket values are random, so the pin is prefix + seams + suffix.
  EXPECT_TRUE(response.body.starts_with(R"({"playerId":"player-1","resumeToken":")"))
      << response.body;
  EXPECT_NE(response.body.find(R"(","ticket":")"), std::string::npos) << response.body;
  EXPECT_TRUE(response.body.ends_with(R"("})")) << response.body;

  const json body = json::parse(response.body);
  std::set<std::string> keys;
  for (const auto& [key, value] : body.items()) keys.insert(key);
  EXPECT_EQ(keys, (std::set<std::string>{"playerId", "resumeToken", "ticket"}));
  EXPECT_FALSE(body["ticket"].get<std::string>().empty());
  EXPECT_FALSE(body["resumeToken"].get<std::string>().empty());
}

// Consumer: the golf web client's reconnect flow. It stores resumeToken
// and replays it as {"resumeToken": ...}; the hub must return the same
// playerId and echo the same token (the long-lived credential must not
// churn), while minting a fresh single-use ticket.
TEST_F(GolfHubWireTest, ResumeTokenRoundTripsThroughTheRawWire) {
  const json first = MintSession();

  const std::string resume_body =
      std::string(R"({"resumeToken":")") + first["resumeToken"].get<std::string>() + R"("})";
  const auto response = PostSession(resume_body);
  ASSERT_EQ(response.status, 200) << response.body;

  const json resumed = json::parse(response.body);
  EXPECT_EQ(resumed["playerId"], first["playerId"]);
  EXPECT_EQ(resumed["resumeToken"], first["resumeToken"]);
  EXPECT_NE(resumed["ticket"], first["ticket"]);
}

// Consumer: the golf web client's Play dial error handling. On this wire
// an unspendable ticket is refused AFTER the upgrade as one terminal
// exception frame, then a clean close — exactly what the browser's
// event-stream decoder must surface as its dial failure. (Production
// main.cc additionally refuses obviously-bad tickets pre-101 with a 401
// gate; a ticket that expires between that peek and the handler's spend
// still lands here, so this frame is the stream's own refusal contract.)
TEST_F(GolfHubWireTest, InvalidTicketRefusesWithTerminalUnauthenticatedFrame) {
  auto socket = DialPlay("?ticket=bogus");

  const auto frame = NextFrame(*socket);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "exception");
  EXPECT_EQ(HeaderText(*frame, ":exception-type"), "Unauthenticated");
  EXPECT_EQ(HeaderText(*frame, ":content-type"), "application/json");
  EXPECT_EQ(frame->headers.size(), 3u);
  EXPECT_EQ(frame->payload.ToString(), R"({"message":"ticket expired or already spent"})");

  // The exception is terminal: the server closes and nothing follows.
  auto closed = socket->Receive(kReceiveBudget);
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(closed->has_value()) << "expected a clean close after the exception frame";
}

// Consumer: the golf web client's whole happy path, raw end to end: mint
// a session over HTTP, dial Play with the ticket, read sessionReady, send
// createRoom, read roomState. Pins the envelope headers and the exact
// payload bytes (key names AND values are deterministic under the
// sequential id generator) for both server events, plus the command
// encoding the client sends.
TEST_F(GolfHubWireTest, ValidSessionStreamsSessionReadyAndRoomStateFrames) {
  const json session = MintSession();
  auto socket = DialPlay("?ticket=" + session["ticket"].get<std::string>());

  // Frame 1: sessionReady, addressed to the freshly minted player. A
  // fresh (non-resumed) seat has no roomId member at all.
  const auto ready = NextFrame(*socket);
  ASSERT_TRUE(ready.has_value());
  EXPECT_EQ(HeaderText(*ready, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*ready, ":event-type"), "sessionReady");
  EXPECT_EQ(HeaderText(*ready, ":content-type"), "application/json");
  EXPECT_EQ(ready->payload.ToString(), R"({"playerId":"player-1","resumed":false})");

  // The createRoom command, framed exactly as the generated client (and
  // the browser wire) encode it.
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());

  // Frame 2: the creator's roomState snapshot — the room id, the member
  // roster with per-player lobby stats, and the (empty) games list.
  const auto room = NextFrame(*socket);
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(HeaderText(*room, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*room, ":event-type"), "roomState");
  EXPECT_EQ(HeaderText(*room, ":content-type"), "application/json");
  EXPECT_EQ(room->payload.ToString(),
            R"({"games":[],"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","totalScore":0}],"roomId":"room-1"})");
}

// Consumer: the golf web client's in-band error toast. A syntactically
// valid command the hub refuses (joining a room that does not exist)
// comes back as a commandRejected EVENT — not an exception, the stream
// survives — whose payload carries the one "reason" key the UI renders.
TEST_F(GolfHubWireTest, SemanticallyInvalidCommandYieldsCommandRejectedEvent) {
  const json session = MintSession();
  auto socket = DialPlay("?ticket=" + session["ticket"].get<std::string>());
  const auto ready = NextFrame(*socket);
  ASSERT_TRUE(ready.has_value());
  ASSERT_EQ(HeaderText(*ready, ":event-type"), "sessionReady");

  ASSERT_TRUE(socket->Send(CommandFrame("joinRoom", R"({"roomId":"ZZZZ99"})")).ok());

  const auto rejected = NextFrame(*socket);
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(HeaderText(*rejected, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*rejected, ":event-type"), "commandRejected");
  EXPECT_EQ(HeaderText(*rejected, ":content-type"), "application/json");
  EXPECT_EQ(rejected->payload.ToString(), R"({"reason":"room unavailable or already in a room"})");

  // The stream survived the rejection: the next command still lands.
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
  const auto room = NextFrame(*socket);
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(HeaderText(*room, ":event-type"), "roomState");
}

}  // namespace
}  // namespace golf_hub
