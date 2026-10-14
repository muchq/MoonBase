// Wire-contract goldens for golf on the room stream, the way
// hub_wire_test pins the room layer's: raw eventstream frames and exact
// payload bytes, because the typed-client suites regenerate both sides
// together and cannot see a rename. The pinned surface: the golf command
// envelope ({"move":{...}} inside the `golf` command), the update
// envelope ({"update":{...}} inside the `golf` event) through
// createGame → gameCreated/gameJoined, joinGame → gameJoined/gameState,
// startGame → gameStarted plus the dealt opening GameView (the full key
// set, card rank/suit spelling included), and the lobby's roomState
// naming the table's game. Updates this suite does not drive
// (peek/draw/swap/knock, turnChanged, gameEnded) share GameView's pinned
// shape but keep their byte-level coverage in golf_e2e_test's and
// hub_e2e_test's typed assertions only.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "opal/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

// Golf rides the room stream on the one route the deployed client dials.
constexpr char kPlayPath[] = "/games/v2/play";

class GolfWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

// Consumer: the golf web client's game UI, whose whole vocabulary is the
// golf envelope — {"move":{...}} up inside the `golf` command,
// {"update":{...}} down inside the `golf` event. Under NoShuffleDealer +
// SequentialIdGenerator every byte is deterministic, so the goldens pin
// the GolfUpdate member names (gameCreated, gameJoined, gameState,
// gameStarted) and the full GameView key set: the waiting view on
// create/join, and the dealt opening view — currentPlayerId, discardTop,
// and the card rank/suit spelling — on start.
TEST_F(GolfWireTest, GameFlowPinsGolfCommandAndUpdatePayloadBytes) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  // createGame, framed exactly as the browser mints it. The creator hears
  // the room-wide announcement, then their own seat (a gameJoined with the
  // waiting-phase view: four face-down slots, no optional members), then
  // the lobby list gaining the game and the creator's table. A member
  // at no table carries no table key at all (the goldens above).
  ASSERT_TRUE(creator->Send(CommandFrame("golf", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameJoined":{"view":{"allPlayersPeeked":false,"discardCount":0,)"
            R"("drawPileCount":0,"gameId":"GAME01","phase":"waiting","players":[)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1",)"
            R"("revealedIndexes":[]}]}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"golf","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"golf","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");

  // A second session joins the room (that admission sequence is pinned in
  // the chat-ordering test) and then the game.
  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(
      joiner->Send(CommandFrame("golf", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  const std::string waiting_pair =
      R"("view":{"allPlayersPeeked":false,"discardCount":0,)"
      R"("drawPileCount":0,"gameId":"GAME01","phase":"waiting","players":[)"
      R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1","revealedIndexes":[]},)"
      R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-2","revealedIndexes":[]}]})";
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "golf"),
            R"({"update":{"gameJoined":{)" + waiting_pair + R"(}}})");
  // The sitting player hears the seat fill as a gameState of the same view.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameState":{)" + waiting_pair + R"(}}})");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // startGame: the bare gameStarted marker, then the dealt opening view —
  // the frame that carries the rest of GameView's keys. Both hands stay
  // face down even to their owners; only the seeded discard shows a face.
  ASSERT_TRUE(creator->Send(CommandFrame("golf", R"({"move":{"startGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"), R"({"update":{"gameStarted":{}}})");
  const auto opening = NextFrame(*creator);
  const std::string opening_payload = EventPayload(opening, "golf");
  EXPECT_EQ(opening_payload,
            R"({"update":{"gameState":{"view":{"allPlayersPeeked":false,)"
            R"("currentPlayerId":"player-1","discardCount":1,)"
            R"("discardTop":{"rank":"Q","suit":"♠"},"drawPileCount":43,"gameId":"GAME01",)"
            R"("phase":"playing","players":[)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1",)"
            R"("revealedIndexes":[]},)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-2",)"
            R"("revealedIndexes":[]}]}}}})");
  // The same pin, spelled as the key set the UI destructures — so a
  // failure names the missing/renamed member even if bytes drift for an
  // unrelated reason first.
  const json view = json::parse(opening_payload)["update"]["gameState"]["view"];
  EXPECT_EQ(KeysOf(view),
            (std::set<std::string>{"allPlayersPeeked", "currentPlayerId", "discardCount",
                                   "discardTop", "drawPileCount", "gameId", "phase", "players"}));
  EXPECT_EQ(KeysOf(view["players"][0]),
            (std::set<std::string>{"cards", "hasPeeked", "playerId", "revealedIndexes"}));
  EXPECT_EQ(KeysOf(view["discardTop"]), (std::set<std::string>{"rank", "suit"}));

  // The lobby list flips to playing for everyone in the room, and both
  // members read as at it: the table key is the lobby's.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"golf","gameId":"GAME01","playerCount":2,"status":"playing"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"golf","gameId":"GAME01"},)"
            R"("totalScore":0},)"
            R"({"connected":true,"gamesPlayed":0,"gamesWon":0,"playerId":"player-2",)"
            R"("table":{"game":"golf","gameId":"GAME01"},"totalScore":0}],"roomId":"room-1"})");
}

}  // namespace
}  // namespace games_hub
