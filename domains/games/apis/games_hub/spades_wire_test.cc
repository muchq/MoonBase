// Wire-contract goldens for spades on the room stream, the way
// pong_wire_test pins pong's: exact payload bytes, because the
// typed-client suites regenerate both sides together and cannot see a
// rename, and the browser reading these lives in another repository. The
// pinned surface: the spades command envelope ({"move":{...}} inside the
// `spades` command) and the update envelope ({"update":{...}} inside the
// `spades` event) through createGame, startGame in partners, a bid, a
// play, the deal refused mid-hand, and the leave that abandons the game.

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"

namespace games_hub {
namespace {

using json = nlohmann::json;
using Socket = std::shared_ptr<opal::http::WebSocket>;

class SpadesWireTest : public HubWireFixture {
 protected:
  // The next frame on `socket` of `event_type`, skipping any other.
  static std::string Next(opal::http::WebSocket& socket, const std::string& event_type) {
    for (int i = 0; i < 32; ++i) {
      const auto frame = NextFrame(socket);
      if (!frame.has_value()) return "<no frame>";
      if (HeaderText(*frame, ":event-type") == event_type) return frame->payload.ToString();
    }
    return "<none>";
  }
  static std::string Spades(opal::http::WebSocket& socket) { return Next(socket, "spades"); }
  static std::string Case(const std::string& payload) {
    return json::parse(payload)["update"].begin().key();
  }
  // The next spades view holding `text`, skipping older ones.
  static std::string ViewWith(opal::http::WebSocket& socket, const std::string& text) {
    for (int i = 0; i < 16; ++i) {
      const std::string payload = SpadesCase(socket, "gameState");
      if (payload.front() != '{' || payload.find(text) != std::string::npos) return payload;
    }
    return "<none>";
  }
  // The next spades update of `wanted`.
  static std::string SpadesCase(opal::http::WebSocket& socket, const std::string& wanted) {
    for (int i = 0; i < 16; ++i) {
      const std::string payload = Spades(socket);
      if (payload.front() != '{') return payload;
      if (Case(payload) == wanted) return payload;
    }
    return "<none>";
  }
};

TEST_F(SpadesWireTest, TableFlowPinsSpadesCommandAndUpdatePayloadBytes) {
  json session;
  std::array<Socket, 4> seats;
  seats[0] = DialReady(kPlayPath, session);
  ASSERT_TRUE(seats[0]->Send(CommandFrame("createRoom", "{}")).ok());
  (void)Next(*seats[0], "roomState");

  ASSERT_TRUE(seats[0]->Send(CommandFrame("spades", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(Spades(*seats[0]),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(
      Spades(*seats[0]),
      R"({"update":{"gameJoined":{"view":{"gameId":"GAME01","handNumber":0,"hands":[],"legal":[],)"
      R"("phase":"waiting","players":[{"hand":[],"handCount":0,"playerId":"player-1",)"
      R"("team":0,"tricks":0}],"spadesBroken":false,"teams":[],)"
      R"("trick":[]}}}})");

  for (int i = 1; i < 4; ++i) {
    json joined;
    seats[i] = DialReady(kPlayPath, joined);
    ASSERT_TRUE(seats[i]->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
    (void)Next(*seats[i], "roomState");
    ASSERT_TRUE(seats[i]
                    ->Send(CommandFrame("spades", R"({"move":{"joinGame":{"gameId":"GAME01"}}})"))
                    .ok());
    (void)SpadesCase(*seats[i], "gameJoined");
  }

  ASSERT_TRUE(
      seats[0]->Send(CommandFrame("spades", R"({"move":{"startGame":{"mode":"partners"}}})")).ok());
  EXPECT_EQ(SpadesCase(*seats[1], "gameStarted"), R"({"update":{"gameStarted":{}}})");
  // Seat 1, at the dealer's left, bids first; it holds every club.
  EXPECT_EQ(
      SpadesCase(*seats[1], "gameState"),
      R"({"update":{"gameState":{"view":{"currentPlayerId":"player-2","dealer":"player-1",)"
      R"("gameId":"GAME01","handNumber":1,"handSize":13,"hands":[],"legal":[],"mode":"partners",)"
      R"("phase":"bidding","players":[)"
      R"({"hand":[],"handCount":13,"playerId":"player-1","team":0,"tricks":0},)"
      R"({"hand":[{"rank":"2","suit":"♣"},{"rank":"3","suit":"♣"},{"rank":"4","suit":"♣"},{"rank":"5","suit":"♣"},{"rank":"6","suit":"♣"},{"rank":"7","suit":"♣"},{"rank":"8","suit":"♣"},{"rank":"9","suit":"♣"},{"rank":"10","suit":"♣"},{"rank":"J","suit":"♣"},{"rank":"Q","suit":"♣"},{"rank":"K","suit":"♣"},{"rank":"A","suit":"♣"}],)"
      R"("handCount":13,"playerId":"player-2","team":1,"tricks":0},)"
      R"({"hand":[],"handCount":13,"playerId":"player-3","team":0,"tricks":0},)"
      R"({"hand":[],"handCount":13,"playerId":"player-4","team":1,"tricks":0}],)"
      R"("spadesBroken":false,"teams":[)"
      R"({"bags":0,"gamesWon":0,"players":["player-1","player-3"],"score":0},)"
      R"({"bags":0,"gamesWon":0,"players":["player-2","player-4"],"score":0}],"trick":[]}}}})");

  ASSERT_TRUE(seats[1]->Send(CommandFrame("spades", R"({"move":{"bid":{"tricks":3}}})")).ok());
  EXPECT_EQ(SpadesCase(*seats[1], "turnChanged"),
            R"({"update":{"turnChanged":{"playerId":"player-3"}}})");
  for (int seat : {2, 3, 0}) {
    ASSERT_TRUE(seats[seat]->Send(CommandFrame("spades", R"({"move":{"bid":{"tricks":0}}})")).ok());
    (void)SpadesCase(*seats[seat], "gameState");
  }

  ASSERT_TRUE(
      seats[1]
          ->Send(CommandFrame("spades", R"({"move":{"play":{"card":{"rank":"2","suit":"♣"}}}})"))
          .ok());
  // Seat 2 sees the trick and every bid, and its own hand only.
  const std::string trick = ViewWith(*seats[2], R"("trick":[{)");
  const json view = json::parse(trick)["update"]["gameState"]["view"];
  EXPECT_EQ(view["phase"], "playing");
  EXPECT_EQ(view["currentPlayerId"], "player-3");
  EXPECT_EQ(view["trick"].dump(), R"([{"card":{"rank":"2","suit":"♣"},"playerId":"player-2"}])");
  EXPECT_EQ(view["players"][1]["bid"], 3);
  EXPECT_EQ(view["players"][2]["bid"], 0);
  EXPECT_EQ(view["players"][1]["handCount"], 12);
  EXPECT_EQ(view["players"][1]["hand"].dump(), "[]");
  EXPECT_EQ(view["legal"].size(), 13u);
  EXPECT_EQ(KeysOf(view),
            (std::set<std::string>{"currentPlayerId", "dealer", "gameId", "handNumber", "handSize",
                                   "legal", "mode", "phase", "players", "hands", "spadesBroken",
                                   "teams", "trick"}));

  ASSERT_TRUE(seats[2]->Send(CommandFrame("spades", R"({"move":{"deal":{}}})")).ok());
  EXPECT_EQ(Next(*seats[2], "commandRejected"), R"({"reason":"the hand is not over"})");

  ASSERT_TRUE(seats[3]->Send(CommandFrame("spades", R"({"move":{"leaveGame":{}}})")).ok());
  EXPECT_EQ(SpadesCase(*seats[3], "gameLeft"), R"({"update":{"gameLeft":{"gameId":"GAME01"}}})");
  EXPECT_EQ(
      SpadesCase(*seats[0], "gameEnded"),
      R"({"update":{"gameEnded":{"result":{"ending":"abandoned","scores":[0,0],"winners":[]}}}})");
}

}  // namespace
}  // namespace games_hub
