// Wire-contract goldens for go on the room stream, the way chess_wire_test
// pins chess's: exact payload bytes, because the typed-client suites
// regenerate both sides together and cannot see a rename, and the browser
// reading these lives in another repository. The pinned surface: the go
// command envelope ({"move":{...}} inside the `go` command) and the update
// envelope ({"update":{...}} inside the `go` event) through createGame,
// joinGame, startGame naming the board, komi and clock, a play, two
// passes and the scored gameEnded; resign in the next game; and the
// model's bounds refused in band.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/wire_test_fixture.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

class GoWireTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    hub_->SetClock([] { return absl::FromUnixMillis(1'800'000'000'000); });
  }
  // The next frame on `socket` of `event_type`, skipping any other.
  static std::string Next(opal::http::WebSocket& socket, const std::string& event_type) {
    for (int i = 0; i < 32; ++i) {
      const auto frame = NextFrame(socket);
      if (!frame.has_value()) return "<no frame>";
      if (HeaderText(*frame, ":event-type") == event_type) return frame->payload.ToString();
    }
    return "<none>";
  }
  // The next go update of `wanted`, skipping other go updates.
  static std::string GoCase(opal::http::WebSocket& socket, const std::string& wanted) {
    for (int i = 0; i < 16; ++i) {
      const std::string payload = Next(socket, "go");
      if (payload.front() != '{') return payload;
      if (json::parse(payload)["update"].begin().key() == wanted) return payload;
    }
    return "<none>";
  }

  // A room of two at one go table, player-1 its creator; neither started.
  std::pair<std::shared_ptr<opal::http::WebSocket>, std::shared_ptr<opal::http::WebSocket>>
  SeatedPair() {
    json creator_session;
    auto creator = DialReady(kPlayPath, creator_session);
    EXPECT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
    (void)Next(*creator, "roomState");
    EXPECT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"createGame":{}}})")).ok());
    (void)GoCase(*creator, "gameJoined");
    json joiner_session;
    auto joiner = DialReady(kPlayPath, joiner_session);
    EXPECT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
    (void)Next(*joiner, "roomState");
    EXPECT_TRUE(
        joiner->Send(CommandFrame("go", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
    (void)GoCase(*joiner, "gameJoined");
    (void)GoCase(*creator, "gameState");
    return {creator, joiner};
  }
};

TEST_F(GoWireTest, TableFlowPinsGoCommandAndUpdatePayloadBytes) {
  json creator_session;
  auto creator = DialReady(kPlayPath, creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)Next(*creator, "roomState");

  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(Next(*creator, "go"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(Next(*creator, "go"),
            R"({"update":{"gameJoined":{"view":{"board":[],"gameId":"GAME01","moveCount":0,)"
            R"("phase":"waiting","players":[{"playerId":"player-1"}],"scoreSheet":[]}}}})");
  EXPECT_EQ(Next(*creator, "roomState"),
            R"({"games":[{"game":"go","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"go","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");

  json joiner_session;
  auto joiner = DialReady(kPlayPath, joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)Next(*joiner, "roomState");
  ASSERT_TRUE(
      joiner->Send(CommandFrame("go", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  EXPECT_EQ(GoCase(*joiner, "gameJoined"),
            R"({"update":{"gameJoined":{"view":{"board":[],"gameId":"GAME01","moveCount":0,)"
            R"("phase":"waiting","players":[{"playerId":"player-1"},{"playerId":"player-2"}],)"
            R"("scoreSheet":[]}}}})");

  ASSERT_TRUE(creator
                  ->Send(CommandFrame("go", R"({"move":{"startGame":{"size":9,"komi":0.5,)"
                                            R"("initialSeconds":60,"incrementSeconds":2}}})"))
                  .ok());
  EXPECT_EQ(GoCase(*creator, "gameStarted"), R"({"update":{"gameStarted":{}}})");
  const std::string empty_rows = R"(".........",".........",".........",".........",".........",)"
                                 R"(".........",".........",".........",".........")";
  EXPECT_EQ(GoCase(*creator, "gameState"),
            R"({"update":{"gameState":{"view":{"board":[)" + empty_rows +
                R"(],"captures":{"black":0,"white":0},)"
                R"("clock":{"blackMs":60000,"incrementMs":2000,"initialMs":60000,"whiteMs":60000},)"
                R"("currentPlayerId":"player-1","gameId":"GAME01","komi":0.5,"moveCount":0,)"
                R"("phase":"playing","players":[{"color":"black","playerId":"player-1"},)"
                R"({"color":"white","playerId":"player-2"}],"scoreSheet":[],)"
                R"("sideToMove":"black","size":9}}}})");

  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"play":{"x":2,"y":1}}})")).ok());
  EXPECT_EQ(GoCase(*joiner, "turnChanged"),
            R"({"update":{"turnChanged":{"playerId":"player-2"}}})");
  const std::string after_play = GoCase(*creator, "gameState");
  const json view = json::parse(after_play)["update"]["gameState"]["view"];
  EXPECT_EQ(view["board"][1], "..b......");
  EXPECT_EQ(view["lastMove"].dump(), R"({"color":"black","pass":false,"x":2,"y":1})");
  EXPECT_EQ(view["clock"]["blackMs"], 62000);
  EXPECT_EQ(view["currentPlayerId"], "player-2");
  EXPECT_EQ(view["moveCount"], 1);

  ASSERT_TRUE(joiner->Send(CommandFrame("go", R"({"move":{"pass":{}}})")).ok());
  EXPECT_EQ(
      json::parse(GoCase(*creator, "gameState"))["update"]["gameState"]["view"]["lastMove"].dump(),
      R"({"color":"white","pass":true})");
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"pass":{}}})")).ok());
  const json ended = json::parse(GoCase(*creator, "gameState"))["update"]["gameState"]["view"];
  EXPECT_EQ(ended["phase"], "ended");
  EXPECT_EQ(ended["result"].dump(),
            R"({"ending":"score","score":{"black":81.0,"white":0.5},"winner":"player-1",)"
            R"("winnerColor":"black"})");
  EXPECT_EQ(ended["scoreSheet"].dump(), R"([{"ending":"score","winner":"player-1"}])");
  EXPECT_FALSE(ended.contains("sideToMove"));
  EXPECT_FALSE(ended.contains("currentPlayerId"));
  EXPECT_EQ(GoCase(*creator, "gameEnded"),
            R"({"update":{"gameEnded":{"result":{"ending":"score",)"
            R"("score":{"black":81.0,"white":0.5},"winner":"player-1","winnerColor":"black"}}}})");

  (void)GoCase(*joiner, "gameEnded");

  // The next game, colours swapped, ends by resignation.
  ASSERT_TRUE(joiner->Send(CommandFrame("go", R"({"move":{"startGame":{}}})")).ok());
  const json next = json::parse(GoCase(*creator, "gameState"))["update"]["gameState"]["view"];
  EXPECT_EQ(next["size"], 19);
  EXPECT_EQ(next["komi"], 6.5);
  EXPECT_EQ(next["players"].dump(),
            R"([{"color":"black","playerId":"player-2"},{"color":"white","playerId":"player-1"}])");
  EXPECT_EQ(next["clock"]["initialMs"], 600000);
  EXPECT_EQ(next["clock"]["incrementMs"], 5000);
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"resign":{}}})")).ok());
  EXPECT_EQ(GoCase(*joiner, "gameEnded"),
            R"({"update":{"gameEnded":{"result":{"ending":"resignation","winner":"player-2",)"
            R"("winnerColor":"black"}}}})");
}

// The model's bounds are the decoder's: refused in band, and the game does
// not start. A size the bounds admit but the rules do not is the rules'.
TEST_F(GoWireTest, TheModelsBoundsAreRefusedInBand) {
  auto [creator, joiner] = SeatedPair();
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"startGame":{"size":21}}})")).ok());
  EXPECT_EQ(Next(*creator, "commandRejected"),
            R"({"reason":"Value at '/go/move/startGame/size' failed to satisfy )"
            R"(constraint: Member must be between 9 and 19, inclusive"})");
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"startGame":{"size":10}}})")).ok());
  EXPECT_EQ(Next(*creator, "commandRejected"), R"({"reason":"a board is 9, 13 or 19, not 10"})");
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"startGame":{"size":9}}})")).ok());
  (void)GoCase(*creator, "gameState");
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"play":{"x":19,"y":0}}})")).ok());
  EXPECT_EQ(Next(*creator, "commandRejected"),
            R"({"reason":"Value at '/go/move/play/x' failed to satisfy )"
            R"(constraint: Member must be between 0 and 18, inclusive"})");
  ASSERT_TRUE(creator->Send(CommandFrame("go", R"({"move":{"play":{"x":9,"y":0}}})")).ok());
  EXPECT_EQ(Next(*creator, "commandRejected"), R"({"reason":"off the board"})");
}

}  // namespace
}  // namespace games_hub
