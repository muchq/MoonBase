// Wire-contract goldens for pong on the room stream, the way
// chess_wire_test pins chess's: exact payload bytes, because the
// typed-client suites regenerate both sides together and cannot see a
// rename, and the browser reading these lives in another repository. The
// pinned surface: the pong command envelope ({"move":{...}} inside the
// `pong` command) and the update envelope ({"update":{...}} inside the
// `pong` event) through createGame, addBot, startGame naming the variant,
// a frame, a paddle, and the leave that abandons the game.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/wire_test_fixture.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

class PongWireTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    hub_->SetClock([] { return absl::FromUnixMillis(1'800'000'000'000); });
  }
};

TEST_F(PongWireTest, TableFlowPinsPongCommandAndUpdatePayloadBytes) {
  json session;
  auto alice = DialReady(kPlayPath, session);
  ASSERT_TRUE(alice->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*alice), "roomState");

  ASSERT_TRUE(alice->Send(CommandFrame("pong", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"),
            R"({"update":{"gameJoined":{"view":{"gameId":"GAME01","phase":"waiting",)"
            R"("players":[{"playerId":"player-1"}],"scoreSheet":[]}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*alice), "roomState"),
            R"({"games":[{"game":"pong","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"pong","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");

  ASSERT_TRUE(alice->Send(CommandFrame("pong", R"({"move":{"addBot":{"level":2}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"),
            R"({"update":{"gameState":{"view":{"gameId":"GAME01","phase":"waiting",)"
            R"("players":[{"playerId":"player-1"},{"bot":true,"playerId":"smallcat@2"}],)"
            R"("scoreSheet":[]}}}})");
  (void)EventPayload(NextFrame(*alice), "roomState");

  ASSERT_TRUE(alice->Send(CommandFrame("pong", R"({"move":{"startGame":{"variant":"3d"}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"), R"({"update":{"gameStarted":{}}})");
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"),
            R"({"update":{"gameState":{"view":{"court":{"ballRadius":12.0,"depth":800.0,)"
            R"("height":300.0,"inset":10.0,"paddleHeight":75.0,"paddleWidth":100.0,)"
            R"("playerSpeed":12.0,"ticksPerSecond":60,"width":400.0},"gameId":"GAME01",)"
            R"("phase":"playing","players":[{"lives":3,"playerId":"player-1"},)"
            R"({"bot":true,"lives":3,"playerId":"smallcat@2"}],"scoreSheet":[],)"
            R"("variant":"3d"}}}})");
  (void)EventPayload(NextFrame(*alice), "roomState");

  // A paddle answers nothing; the frame shows where it went.
  ASSERT_TRUE(alice->Send(CommandFrame("pong", R"({"move":{"paddle":{"x":0,"y":0}}})")).ok());
  ASSERT_TRUE(alice->Send(CommandFrame("getRoomState", "{}")).ok());
  (void)EventPayload(NextFrame(*alice), "roomState");
  hub_->TickPongOnce();
  hub_->TickPongOnce();
  EXPECT_EQ(
      EventPayload(NextFrame(*alice), "pong"),
      R"({"update":{"frame":{"ball":{"x":200.0,"y":150.0,"z":400.0},"gameId":"GAME01",)"
      R"("paddles":[{"x":176.0,"y":126.0},{"x":200.0,"y":150.0}],"serveIn":58,"tick":108000000002,)"
      R"("velocity":{"x":0.0,"y":0.0,"z":0.0}}}})");

  ASSERT_TRUE(alice->Send(CommandFrame("pong", R"({"move":{"leaveGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alice), "pong"),
            R"({"update":{"gameLeft":{"gameId":"GAME01"}}})");
}

}  // namespace
}  // namespace games_hub
