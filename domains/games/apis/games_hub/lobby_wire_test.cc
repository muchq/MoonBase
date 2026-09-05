// Beyoncé Rule wire-contract tests for the lobby (#1490 phase 3): golden
// fixtures pinning the one stream's route and the lobby envelope's exact
// bytes, driven through the generated server WITHOUT the generated client
// — a regeneration that renames what the client reads fails here even
// though lobby_e2e_test (which regenerates with it) still passes.
//
// The pinned surface, exactly: the lobby command frames as the client
// mints them, and the lobby events as it reads them; and the terminal
// Unauthenticated frame. The route, the session mint and resume bodies,
// and the room layer's own frames are golf_wire_test's pins.
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

// The one stream's route (#1490); renaming it strands every deployed web
// client.
constexpr char kPlayPath[] = "/games/v2/play";

// The join the lobby sends: the fixture player, in the lobby envelope,
// naming no room.
constexpr char kJoinPayload[] =
    R"({"action":{"join":{"position":[10,0,-5],"color":[0.8,0.2,0.6],"shape":0}}})";

class LobbyWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<smithy::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

// Consumer: the lobby's join and the world it draws, under the `lobby`
// event with the update nested under "update". The first joiner hears an
// empty worldState; the second hears the first as a WorldPlayer (the
// double spelling included — 10.0, not 10), and the first hears the
// second arrive as a playerJoined.
TEST_F(LobbyWireTest, JoinPinsWorldStateAndPlayerJoinedBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("lobby", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"),
            R"({"update":{"worldState":{"players":[]}}})");

  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"position":[20,0,15],)"
                                               R"("color":[0.3,0.9,0.4],"shape":1}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"),
            R"({"update":{"worldState":{"players":[{"color":[0.8,0.2,0.6],)"
            R"("playerId":"player-1","position":[10.0,0.0,-5.0],"shape":0}]}}})");
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"),
            R"({"update":{"playerJoined":{"player":{"color":[0.3,0.9,0.4],)"
            R"("playerId":"player-2","position":[20.0,0.0,15.0],"shape":1}}}})");
}

// Consumer: the lobby's per-frame traffic. A move and a shape change
// reach the other session as playerMoved and shapeChanged naming the
// actor; the actor hears no echo (its next frame is the other's leave),
// and a leave reaches the other as playerLeft.
TEST_F(LobbyWireTest, MoveShapeAndLeavePinTheirBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*first), "lobby");
  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*second), "lobby");
  (void)EventPayload(NextFrame(*first), "lobby");

  ASSERT_TRUE(
      first->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[15,0,-8]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"),
            R"({"update":{"playerMoved":{"playerId":"player-1","position":[15.0,0.0,-8.0]}}})");

  ASSERT_TRUE(first->Send(CommandFrame("lobby", R"({"action":{"shape":{"shape":2}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"),
            R"({"update":{"shapeChanged":{"playerId":"player-1","shape":2}}})");

  ASSERT_TRUE(second->Send(CommandFrame("lobby", R"({"action":{"leave":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"),
            R"({"update":{"playerLeft":{"playerId":"player-2"}}})");
}

// Consumer: the client's in-band error path on this stream. A lobby
// command the world refuses comes back as the stream's commandRejected
// EVENT — the stream survives — with the one "reason" key; the next
// command still lands.
TEST_F(LobbyWireTest, RejectedLobbyCommandsYieldCommandRejectedEvents) {
  json session;
  auto socket = DialReady(session);

  ASSERT_TRUE(
      socket->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[1,0,1]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "commandRejected"),
            R"({"reason":"join the world first"})");
  ASSERT_TRUE(socket
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"roomId":"ABC123",)"
                                               R"("position":[10,0,-5],"color":[0.8,0.2,0.6],)"
                                               R"("shape":0}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "commandRejected"),
            R"({"reason":"the world is your room's; join the room first"})");
  ASSERT_TRUE(socket->Send(CommandFrame("lobby", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "lobby"),
            R"({"update":{"worldState":{"players":[]}}})");
}

// Consumer: the client's dial error handling — the terminal
// Unauthenticated frame and a clean close.
TEST_F(LobbyWireTest, InvalidTicketRefusesWithTerminalUnauthenticatedFrame) {
  auto socket = DialStream(kPlayPath, "?ticket=bogus");

  const auto frame = NextFrame(*socket);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "exception");
  EXPECT_EQ(HeaderText(*frame, ":exception-type"), "Unauthenticated");
  EXPECT_EQ(frame->payload.ToString(), R"({"message":"ticket expired or already spent"})");

  auto closed = socket->Receive(kWireReceiveBudget);
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(closed->has_value()) << "expected a clean close after the exception frame";
}

}  // namespace
}  // namespace games_hub
