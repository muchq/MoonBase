// Wire-contract goldens for chess on the room stream, the way
// castle_wire_test pins castle's: raw eventstream frames and exact payload
// bytes, because the typed-client suites regenerate both sides together
// and cannot see a rename. The pinned surface: the chess command envelope
// ({"move":{...}} inside the `chess` command) and the update envelope
// ({"update":{...}} inside the `chess` event) through createGame, joinGame,
// startGame naming the clock, a play that mates, and gameEnded; the model's
// bounds on startGame and play refused in band; and the lobby's roomState
// naming the table's game.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "opal/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

constexpr char kPlayPath[] = "/games/v2/play";

class ChessWireTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    golf_->SetClock([] { return absl::FromUnixMillis(1'800'000'000'000); });
    // White Kg6 Pe7 against Kh8: e7e8q mates.
    golf_->SetChessOpener([] { return chess_play::Opening{"7k/4P3/6K1/8/8/8/8/8 w - - 0 1", 0}; });
  }
  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

TEST_F(ChessWireTest, TableFlowPinsChessCommandAndUpdatePayloadBytes) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameJoined":{"view":{"gameId":"GAME01","inCheck":false,)"
            R"("legalMoves":[],"moves":[],"phase":"waiting",)"
            R"("players":[{"playerId":"player-1"}]}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"chess","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"chess","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");

  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(
      joiner->Send(CommandFrame("chess", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  const std::string waiting_pair =
      R"("view":{"gameId":"GAME01","inCheck":false,"legalMoves":[],"moves":[],)"
      R"("phase":"waiting","players":[{"playerId":"player-1"},{"playerId":"player-2"}]})";
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "chess"),
            R"({"update":{"gameJoined":{)" + waiting_pair + R"(}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{)" + waiting_pair + R"(}}})");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // startGame names the clock in seconds; the view carries milliseconds.
  ASSERT_TRUE(
      creator
          ->Send(CommandFrame(
              "chess", R"({"move":{"startGame":{"initialSeconds":60,"incrementSeconds":1}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"), R"({"update":{"gameStarted":{}}})");
  const std::string playing =
      R"("view":{"clock":{"blackMs":60000,"incrementMs":1000,"initialMs":60000,)"
      R"("whiteMs":60000},"currentPlayerId":"player-1",)"
      R"("fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","gameId":"GAME01","inCheck":false,)"
      R"("legalMoves":["e7e8b","e7e8n","e7e8q","e7e8r","g6f5","g6f6","g6f7","g6g5",)"
      R"("g6h5","g6h6"],"moves":[],"phase":"playing",)"
      R"("players":[{"color":"white","playerId":"player-1"},)"
      R"({"color":"black","playerId":"player-2"}],"sideToMove":"white","variant":"kpk"})";
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{)" + playing + R"(}}})");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // The mate: the ended view — no side to move, no legal moves, the
  // clock stood still with White's increment — then the result.
  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"play":{"uci":"e7e8q"}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{"view":{"clock":{"blackMs":60000,"incrementMs":1000,)"
            R"("initialMs":60000,"whiteMs":61000},"fen":"4Q2k/8/6K1/8/8/8/8/8 b - - 0 1",)"
            R"("gameId":"GAME01","inCheck":true,"legalMoves":[],"moves":["e7e8q"],"phase":"ended",)"
            R"("players":[{"color":"white","playerId":"player-1"},)"
            R"({"color":"black","playerId":"player-2"}],)"
            R"("result":{"ending":"checkmate","winner":"player-1","winnerColor":"white"},)"
            R"("variant":"kpk"}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameEnded":{"result":{"ending":"checkmate","winner":"player-1",)"
            R"("winnerColor":"white"}}}})");
}

// The model's bounds are the decoder's to enforce: a clock outside them,
// or a move that is not four or five characters, is refused in band
// before the hub sees it — and the table is left as it was.
TEST_F(ChessWireTest, TheModelsBoundsAreRefusedInBand) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");
  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"createGame":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "roomState");

  for (const char* move :
       {R"({"move":{"startGame":{"initialSeconds":29}}})",
        R"({"move":{"startGame":{"initialSeconds":1801}}})",
        R"({"move":{"startGame":{"incrementSeconds":31}}})", R"({"move":{"play":{"uci":"e7e"}}})",
        R"({"move":{"play":{"uci":"e7e8qq"}}})"}) {
    ASSERT_TRUE(creator->Send(CommandFrame("chess", move)).ok());
    (void)EventPayload(NextFrame(*creator), "commandRejected");
  }
  // The control: the same start inside the bounds reaches the hub, which
  // refuses it for its own reason.
  ASSERT_TRUE(
      creator->Send(CommandFrame("chess", R"({"move":{"startGame":{"initialSeconds":30}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"need at least 2 players to start"})");
}

}  // namespace
}  // namespace games_hub
