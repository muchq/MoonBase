// Beyoncé Rule wire-contract tests for the thoughts WEB CLIENT (#79): golden
// fixtures pinning the raw route, eventstream frames, and exact payload
// bytes the browser client at muchq.com/thoughts depends on, driven through
// the generated server WITHOUT the generated client — a regeneration that
// renames what the client reads fails here even though thoughts_e2e_test
// (which regenerates with it) still passes.
//
// The pinned surface, exactly: the Think route; sessionReady; the four
// command payloads (join, move, shape, leave) as the client mints them,
// and join's roomId key the lobby will mint (#1490); worldState empty and
// populated (the double spelling included — 10.0, not
// 10); playerJoined's nested player; playerMoved; shapeChanged; playerLeft;
// commandRejected; and the terminal Unauthenticated frame. The session
// mint and resume bodies are golf_wire_test's pins — one route, one set of
// bytes, whichever stream it opens.
//
// The harness is wire_test_fixture.h's; non-Beast, so it runs with no
// sandbox setup at all.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "smithy/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

// The thoughts stream route; renaming it strands every deployed web client.
constexpr char kThinkPath[] = "/games/v2/thoughts/play";

// The join the client sends for a player standing at (10, 0, -5) in
// magenta-ish as a sphere — the Go server's own test fixture, so the
// numbers double as the v1 protocol's.
constexpr char kJoinPayload[] = R"({"position":[10,0,-5],"color":[0.8,0.2,0.6],"shape":0})";

class ThoughtsWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<smithy::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kThinkPath, session);
  }
};

// Consumer: the client's connect. The first frame names the server-assigned
// player (the welcome of the v1 protocol); resumed is always false here,
// since thoughts parks nothing, and there is no roomId key at all.
TEST_F(ThoughtsWireTest, DialPinsSessionReadyBytes) {
  const json session = MintSession();
  auto socket = DialStream(kThinkPath, "?ticket=" + session["ticket"].get<std::string>());
  const auto ready = NextFrame(*socket);
  EXPECT_EQ(EventPayload(ready, "sessionReady"), R"({"playerId":"player-1","resumed":false})");
}

// Consumer: the client's join and the world it draws. The first joiner
// hears an empty worldState; the second hears the first as a WorldPlayer,
// and the first hears the second arrive as a playerJoined carrying the
// same shape under "player".
TEST_F(ThoughtsWireTest, JoinPinsWorldStateAndPlayerJoinedBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("join", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "worldState"), R"({"players":[]})");

  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("join", R"({"position":[20,0,15],"color":[0.3,0.9,0.4],"shape":1})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "worldState"),
            R"({"players":[{"color":[0.8,0.2,0.6],"playerId":"player-1",)"
            R"("position":[10.0,0.0,-5.0],"shape":0}]})");

  EXPECT_EQ(EventPayload(NextFrame(*first), "playerJoined"),
            R"({"player":{"color":[0.3,0.9,0.4],"playerId":"player-2",)"
            R"("position":[20.0,0.0,15.0],"shape":1}})");
}

// Consumer: the lobby's join (#1490 phase 4). The roomId key on the join
// payload names the world; a joiner naming one sees only that world, so
// the unroomed plaza player above is not in it, and never hears of it.
TEST_F(ThoughtsWireTest, JoinWithRoomIdPinsTheKeyAndScopesTheWorld) {
  json plaza_session;
  auto plaza = DialReady(plaza_session);
  ASSERT_TRUE(plaza->Send(CommandFrame("join", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*plaza), "worldState");

  json roomed_session;
  auto roomed = DialReady(roomed_session);
  ASSERT_TRUE(roomed
                  ->Send(CommandFrame("join", R"({"position":[10,0,-5],"color":[0.8,0.2,0.6],)"
                                              R"("shape":0,"roomId":"ABC123"})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*roomed), "worldState"), R"({"players":[]})");

  ASSERT_TRUE(roomed->Send(CommandFrame("leave", "{}")).ok());
  // The plaza player's next frame is its own later refusal, not any of
  // the roomed player's traffic.
  ASSERT_TRUE(plaza->Send(CommandFrame("join", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*plaza), "commandRejected"),
            R"({"reason":"already in the world; leave first"})");
}

// Consumer: the client's per-frame traffic. A move and a shape change reach
// the other session as playerMoved and shapeChanged naming the actor; the
// actor hears no echo (its next frame is the other's leave), and a leave
// reaches the other as playerLeft.
TEST_F(ThoughtsWireTest, MoveShapeAndLeavePinTheirBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("join", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*first), "worldState");
  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second->Send(CommandFrame("join", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*second), "worldState");
  (void)EventPayload(NextFrame(*first), "playerJoined");

  ASSERT_TRUE(first->Send(CommandFrame("move", R"({"position":[15,0,-8]})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "playerMoved"),
            R"({"playerId":"player-1","position":[15.0,0.0,-8.0]})");

  ASSERT_TRUE(first->Send(CommandFrame("shape", R"({"shape":2})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "shapeChanged"),
            R"({"playerId":"player-1","shape":2})");

  ASSERT_TRUE(second->Send(CommandFrame("leave", "{}")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "playerLeft"), R"({"playerId":"player-2"})");
}

// Consumer: the client's in-band error path. A well-formed command the
// world refuses comes back as a commandRejected EVENT — the stream
// survives — whose payload carries the one "reason" key; the next command
// still lands.
TEST_F(ThoughtsWireTest, RejectedCommandsYieldCommandRejectedEvents) {
  json session;
  auto socket = DialReady(session);

  ASSERT_TRUE(socket->Send(CommandFrame("move", R"({"position":[1,0,1]})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "commandRejected"),
            R"({"reason":"join the world first"})");

  ASSERT_TRUE(
      socket->Send(CommandFrame("join", R"({"position":[100,0,0],"color":[0,0,0],"shape":0})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "commandRejected"),
            R"json({"reason":"position out of bounds (±50)"})json");

  ASSERT_TRUE(socket->Send(CommandFrame("join", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "worldState"), R"({"players":[]})");
}

// Consumer: the client's dial error handling. An unspendable ticket is
// refused after the upgrade as one terminal Unauthenticated exception
// frame, then a clean close — the same envelope golf's dial failures use,
// so one decoder serves both.
TEST_F(ThoughtsWireTest, InvalidTicketRefusesWithTerminalUnauthenticatedFrame) {
  auto socket = DialStream(kThinkPath, "?ticket=bogus");

  const auto frame = NextFrame(*socket);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "exception");
  EXPECT_EQ(HeaderText(*frame, ":exception-type"), "Unauthenticated");
  EXPECT_EQ(HeaderText(*frame, ":content-type"), "application/json");
  EXPECT_EQ(frame->headers.size(), 3u);
  EXPECT_EQ(frame->payload.ToString(), R"({"message":"ticket expired or already spent"})");

  auto closed = socket->Receive(kWireReceiveBudget);
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(closed->has_value()) << "expected a clean close after the exception frame";
}

}  // namespace
}  // namespace games_hub
