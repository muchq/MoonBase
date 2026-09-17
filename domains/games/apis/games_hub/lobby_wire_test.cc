// Beyoncé Rule wire-contract tests for the lobby (#1490 phase 3): golden
// fixtures pinning the one stream's route and the lobby envelope's exact
// bytes, driven through the generated server WITHOUT the generated client
// — a regeneration that renames what the client reads fails here even
// though lobby_e2e_test (which regenerates with it) still passes.
//
// The pinned surface, exactly: the lobby command frames as the client
// mints them, and the lobby events as it reads them; and the terminal
// Unauthenticated frame. The route, the session mint and resume bodies,
// and the room layer's own frames are golf_wire_test's pins; the tape on
// a glasshouse's glass is tape_test's, which needs a deja to poll.
//
// The harness is wire_test_fixture.h's; non-Beast, so it runs with no
// sandbox setup at all.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "opal/http/message.h"

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
  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

// Consumer: the lobby's join and the world it draws, under the `lobby`
// event with the update nested under "update". The first joiner hears an
// empty worldState naming the plaza's plane; the second hears the first
// as a WorldPlayer (the double spelling included — 10.0, not 10), and
// the first hears the second arrive as a playerJoined.
TEST_F(LobbyWireTest, JoinPinsWorldStateAndPlayerJoinedBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("lobby", kJoinPayload)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"),
            R"({"update":{"worldState":{"geometry":{"plane":{}},"players":[]}}})");

  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"position":[20,0,15],)"
                                               R"("color":[0.3,0.9,0.4],"shape":1}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"),
            R"({"update":{"worldState":{"geometry":{"plane":{}},"players":[{"color":[0.8,0.2,0.6],)"
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
            R"({"update":{"worldState":{"geometry":{"plane":{}},"players":[]}}})");
}

// Consumer: a room created on a sphere (#1554). The creator's worldState
// names the sphere and its radius; a join or move within an avatar's
// reach of the wall lands ON it (the fan-out carries the snapped
// position, not the sent one), one farther off is refused with the
// radius in the reason, and a radius too small to stand in refuses the
// createRoom itself.
TEST_F(LobbyWireTest, SphereRoomPinsGeometrySnapAndRefusalBytes) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(
      creator->Send(CommandFrame("createRoom", R"({"geometry":{"sphere":{"radius":53}}})")).ok());
  // The room names its sphere before anyone has to guess a position.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[],"geometry":{"sphere":{"radius":53.0}},"players":[{"connected":true,)"
            R"("gamesPlayed":0,"gamesWon":0,"playerId":"player-1","totalScore":0}],)"
            R"("roomId":"room-1"})");
  ASSERT_TRUE(creator
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"roomId":"room-1",)"
                                               R"("position":[0,0,-52.5],"color":[0.8,0.2,0.6],)"
                                               R"("shape":0}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "lobby"),
            R"({"update":{"worldState":{"geometry":{"sphere":{"radius":53.0}},"players":[]}}})");

  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");
  ASSERT_TRUE(joiner
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"roomId":"room-1",)"
                                               R"("position":[53,0,0],"color":[0.3,0.9,0.4],)"
                                               R"("shape":1}}})"))
                  .ok());
  // The creator stands where the wall is, not where the join said.
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "lobby"),
            R"({"update":{"worldState":{"geometry":{"sphere":{"radius":53.0}},)"
            R"("players":[{"color":[0.8,0.2,0.6],"playerId":"player-1",)"
            R"("position":[0.0,0.0,-53.0],"shape":0}]}}})");
  (void)EventPayload(NextFrame(*creator), "lobby");

  ASSERT_TRUE(
      creator->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[0,0,-52]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "lobby"),
            R"({"update":{"playerMoved":{"playerId":"player-1","position":[0.0,0.0,-53.0]}}})");
  ASSERT_TRUE(
      creator->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[0,0,-40]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"~({"reason":"position must be on the sphere (radius 53)"})~");

  json other_session;
  auto other = DialReady(other_session);
  ASSERT_TRUE(
      other->Send(CommandFrame("createRoom", R"({"geometry":{"sphere":{"radius":1}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*other), "commandRejected"),
            R"({"reason":"sphere radius must be within 2..1000"})");
}

// Consumer: the room changing shape under everyone (#1554). A member's
// setGeometry reaches every seat standing in the world, the actor
// included, as one geometryChanged carrying the surface and everyone's
// placement on it; a move then obeys the new surface; a geometry the
// hub cannot host is refused in band and changes nothing.
TEST_F(LobbyWireTest, SetGeometryPinsGeometryChangedBytes) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*first), "lobby");
  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"position":[0,0,0],)"
                                               R"("color":[0.3,0.9,0.4],"shape":1}}})"))
                  .ok());
  (void)EventPayload(NextFrame(*second), "lobby");
  (void)EventPayload(NextFrame(*first), "lobby");

  ASSERT_TRUE(first
                  ->Send(CommandFrame("lobby", R"({"action":{"setGeometry":{"geometry":)"
                                               R"({"sphere":{"radius":5}}}}})"))
                  .ok());
  const std::string changed =
      R"({"update":{"geometryChanged":{"geometry":{"sphere":{"radius":5.0}},"players":[)"
      R"({"color":[0.8,0.2,0.6],"playerId":"player-1","position":[4.47213595499958,0.0,)"
      R"(-2.23606797749979],"shape":0},{"color":[0.3,0.9,0.4],"playerId":"player-2",)"
      R"("position":[0.0,0.0,-5.0],"shape":1}]}}})";
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"), changed);
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"), changed);

  ASSERT_TRUE(
      second->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[0,0,-4.5]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"),
            R"({"update":{"playerMoved":{"playerId":"player-2","position":[0.0,0.0,-5.0]}}})");
  ASSERT_TRUE(second
                  ->Send(CommandFrame("lobby", R"({"action":{"setGeometry":{"geometry":)"
                                               R"({"sphere":{"radius":1}}}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "commandRejected"),
            R"({"reason":"sphere radius must be within 2..1000"})");
}

// Consumer: a room on the glasshouse (#1554). The geometry rides the wire
// as its own empty case — no wall height, because the client picks that —
// and the floor underneath is the plane's to the byte: the same positions
// land, and the same ones come back refused with the plane's reason.
TEST_F(LobbyWireTest, GlasshouseRoomPinsGeometryAndKeepsThePlanesFloor) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", R"({"geometry":{"glasshouse":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[],"geometry":{"glasshouse":{}},"players":[{"connected":true,)"
            R"("gamesPlayed":0,"gamesWon":0,"playerId":"player-1","totalScore":0}],)"
            R"("roomId":"room-1"})");
  ASSERT_TRUE(creator
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"roomId":"room-1",)"
                                               R"("position":[10,0,-5],"color":[0.8,0.2,0.6],)"
                                               R"("shape":0}}})"))
                  .ok());
  // No tape has run, so the wall is absent rather than an empty list.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "lobby"),
            R"({"update":{"worldState":{"geometry":{"glasshouse":{}},"players":[]}}})");

  // The floor is the plane's: the edge stands, past it does not, and the
  // glass is not a place to be.
  ASSERT_TRUE(
      creator->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[50,0,-50]}}})")).ok());
  ASSERT_TRUE(
      creator->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[51,0,0]}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"position out of bounds (±50)"
})");
  ASSERT_TRUE(
      creator->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[50,12,0]}}})")).ok());
EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"), R"({"reason":"y must be 0"})");
}

// Consumer: a room becoming a glasshouse under everyone standing in it.
// The floor does not move, so every placement is where the player already
// was — the client redraws the walls, not the crowd.
TEST_F(LobbyWireTest, SetGeometryToGlasshouseKeepsEveryoneWhereTheyStand) {
  json first_session;
  auto first = DialReady(first_session);
  ASSERT_TRUE(first->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*first), "lobby");
  json second_session;
  auto second = DialReady(second_session);
  ASSERT_TRUE(second
                  ->Send(CommandFrame("lobby", R"({"action":{"join":{"position":[20,0,15],)"
                                               R"("color":[0.3,0.9,0.4],"shape":1}}})"))
                  .ok());
  (void)EventPayload(NextFrame(*second), "lobby");
  (void)EventPayload(NextFrame(*first), "lobby");

  ASSERT_TRUE(first
                  ->Send(CommandFrame("lobby", R"({"action":{"setGeometry":{"geometry":)"
                                               R"({"glasshouse":{}}}}})"))
                  .ok());
  const std::string changed =
      R"({"update":{"geometryChanged":{"geometry":{"glasshouse":{}},"players":[)"
      R"({"color":[0.8,0.2,0.6],"playerId":"player-1","position":[10.0,0.0,-5.0],"shape":0},)"
      R"({"color":[0.3,0.9,0.4],"playerId":"player-2","position":[20.0,0.0,15.0],"shape":1}]}}})";
  EXPECT_EQ(EventPayload(NextFrame(*first), "lobby"), changed);
  EXPECT_EQ(EventPayload(NextFrame(*second), "lobby"), changed);
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
