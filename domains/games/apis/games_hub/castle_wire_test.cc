// Wire-contract goldens for castle (#77) on the room stream, the way
// golf_wire_test pins golf's: raw eventstream frames and exact payload
// bytes, because the typed-client suites regenerate both sides together
// and cannot see a rename. The pinned surface: the castle command
// envelope ({"move":{...}} inside the `castle` command), the update
// envelope ({"update":{...}} inside the `castle` event) through
// createGame → gameCreated/gameJoined, joinGame → gameJoined/gameState,
// startGame → gameStarted plus the dealt setup CastleView (the full key
// set — own hand faces, the other hand as a count, face-up rows, the
// face-down counts — with the card rank/suit spelling), and the lobby's
// roomState naming the table's game.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "smithy/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

// Castle rides the room stream; there is no castle route to rename.
constexpr char kPlayPath[] = "/games/v2/golf/play";

class CastleWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<smithy::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

TEST_F(CastleWireTest, TableFlowPinsCastleCommandAndUpdatePayloadBytes) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  // createGame in castle's envelope: the room-wide announcement, the
  // creator's seat with the waiting view, then the lobby naming the game.
  ASSERT_TRUE(creator->Send(CommandFrame("castle", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "castle"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  const std::string waiting_one =
      R"("view":{"drawPileCount":0,"finished":[],"gameId":"GAME01","phase":"waiting",)"
      R"("pileCount":0,"players":[{"canPlay":false,"faceDownCount":0,"faceUp":[],"hand":[],"handCount":0,)"
      R"("out":false,"playerId":"player-1","ready":false}]})";
  EXPECT_EQ(EventPayload(NextFrame(*creator), "castle"),
            R"({"update":{"gameJoined":{)" + waiting_one + R"(}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"castle","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","totalScore":0}],"roomId":"room-1"})");

  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(
      joiner->Send(CommandFrame("castle", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  const std::string waiting_pair =
      R"("view":{"drawPileCount":0,"finished":[],"gameId":"GAME01","phase":"waiting",)"
      R"("pileCount":0,"players":[{"canPlay":false,"faceDownCount":0,"faceUp":[],"hand":[],"handCount":0,)"
      R"("out":false,"playerId":"player-1","ready":false},)"
      R"({"canPlay":false,"faceDownCount":0,"faceUp":[],"hand":[],"handCount":0,"out":false,)"
      R"("playerId":"player-2","ready":false}]})";
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "castle"),
            R"({"update":{"gameJoined":{)" + waiting_pair + R"(}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "castle"),
            R"({"update":{"gameState":{)" + waiting_pair + R"(}}})");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // startGame: the bare marker, then the dealt setup view from the
  // creator's chair — their own three faces, the other hand as a count.
  ASSERT_TRUE(creator->Send(CommandFrame("castle", R"({"move":{"startGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "castle"), R"({"update":{"gameStarted":{}}})");
  const std::string setup_payload = EventPayload(NextFrame(*creator), "castle");
  EXPECT_EQ(
      setup_payload,
      R"({"update":{"gameState":{"view":{"drawPileCount":34,"finished":[],)"
      R"("gameId":"GAME01","phase":"setup","pileCount":0,"players":[)"
      R"({"canPlay":false,"faceDownCount":3,"faceUp":[{"rank":"A","suit":"♣"},{"rank":"K","suit":"♠"},)"
      R"({"rank":"K","suit":"♥"}],"hand":[{"rank":"K","suit":"♦"},{"rank":"K","suit":"♣"},)"
      R"({"rank":"Q","suit":"♠"}],"handCount":3,"out":false,"playerId":"player-1",)"
      R"("ready":false},)"
      R"({"canPlay":false,"faceDownCount":3,"faceUp":[{"rank":"J","suit":"♠"},{"rank":"J","suit":"♥"},)"
      R"({"rank":"J","suit":"♦"}],"hand":[],"handCount":3,"out":false,)"
      R"("playerId":"player-2","ready":false}]}}}})");
  const json view = json::parse(setup_payload)["update"]["gameState"]["view"];
  EXPECT_EQ(KeysOf(view), (std::set<std::string>{"drawPileCount", "finished", "gameId", "phase",
                                                 "pileCount", "players"}));
  EXPECT_EQ(KeysOf(view["players"][0]),
            (std::set<std::string>{"canPlay", "faceDownCount", "faceUp", "hand", "handCount", "out",
                                   "playerId", "ready"}));
  // Absent optionals (pileTop, currentPlayerId, lastPlay) are omitted keys, not nulls.
  EXPECT_FALSE(view.contains("pileTop"));
  EXPECT_FALSE(view.contains("currentPlayerId"));
  EXPECT_FALSE(view.contains("lastPlay"));

  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"castle","gameId":"GAME01","playerCount":2,"status":"setup"}],)"
            R"("players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","totalScore":0},)"
            R"({"connected":true,"gamesPlayed":0,"gamesWon":0,"playerId":"player-2",)"
            R"("totalScore":0}],"roomId":"room-1"})");
}

}  // namespace
}  // namespace games_hub
