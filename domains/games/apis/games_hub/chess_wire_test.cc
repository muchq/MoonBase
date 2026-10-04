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
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "opal/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

constexpr char kPlayPath[] = "/games/v2/play";
const std::string kAvailableSetups =
    R"("availableSetups":[{"name":"Standard starting position","setupId":"standard"},)"
    R"({"name":"Random K+P vs K","setupId":"random-kpk"},)"
    R"({"name":"K+P vs K — Opposition","setupId":"kpk-opposition"},)"
    R"({"name":"R+P vs R — Lucena position","setupId":"rpr-lucena"},)"
    R"({"name":"Q vs R — Basic conversion","setupId":"qvr-basic"}],)";

class ChessWireTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    golf_->SetClock([] { return absl::FromUnixMillis(1'800'000'000'000); });
    // White Kg6 Pe7 against Kh8: e7e8q mates.
    golf_->SetChessOpener([this](std::string_view setup_id) {
      auto setup = chess_play::SelectChessSetup(setup_id, setup_gen_);
      if (setup.ok()) {
        setup->opening = chess_play::Opening{"7k/4P3/6K1/8/8/8/8/8 w - - 0 1", 0};
      }
      return setup;
    });
  }
  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }

  std::mt19937_64 setup_gen_{1234};
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
            R"({"update":{"gameJoined":{"view":{)" + kAvailableSetups +
                R"("defaultSetupId":"standard","gameId":"GAME01","inCheck":false,)"
            R"("legalMoves":[],"moves":[],"phase":"waiting",)"
            R"("players":[{"playerId":"player-1"}],"scoreSheet":[]}}}})");
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
      R"("view":{)" + kAvailableSetups +
      R"("defaultSetupId":"standard","gameId":"GAME01","inCheck":false,"legalMoves":[],"moves":[],)"
      R"("phase":"waiting","players":[{"playerId":"player-1"},{"playerId":"player-2"}],)"
      R"("scoreSheet":[]})";
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "chess"),
            R"({"update":{"gameJoined":{)" + waiting_pair + R"(}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{)" + waiting_pair + R"(}}})");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // startGame names the setup and clock; the view carries both.
  ASSERT_TRUE(
      creator
          ->Send(CommandFrame(
              "chess",
              R"({"move":{"startGame":{"initialSeconds":60,"incrementSeconds":1,)"
              R"("setupId":"kpk-opposition"}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"), R"({"update":{"gameStarted":{}}})");
  const std::string playing =
      R"("view":{)" + kAvailableSetups +
      R"("clock":{"blackMs":60000,"incrementMs":1000,"initialMs":60000,)"
      R"("whiteMs":60000},"currentPlayerId":"player-1","defaultSetupId":"standard",)"
      R"("fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","gameId":"GAME01","inCheck":false,)"
      R"("legalMoves":["e7e8b","e7e8n","e7e8q","e7e8r","g6f5","g6f6","g6f7","g6g5",)"
      R"("g6h5","g6h6"],"moves":[],"phase":"playing",)"
      R"("players":[{"color":"white","playerId":"player-1"},)"
      R"({"color":"black","playerId":"player-2"}],"scoreSheet":[],)"
      R"("setupId":"kpk-opposition","setupName":"K+P vs K — Opposition",)"
      R"("sideToMove":"white","variant":"kpk"})";
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{)" + playing + R"(}}})");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // The mate: the ended view — no side to move, no legal moves, the
  // clock stood still with White's increment — then the result.
  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"play":{"uci":"e7e8q"}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameState":{"view":{)" + kAvailableSetups +
                R"("clock":{"blackMs":60000,"incrementMs":1000,)"
            R"("initialMs":60000,"whiteMs":61000},"defaultSetupId":"standard",)"
            R"("fen":"4Q2k/8/6K1/8/8/8/8/8 b - - 0 1",)"
            R"("gameId":"GAME01","inCheck":true,"legalMoves":[],"moves":["e7e8q"],"phase":"ended",)"
            R"("players":[{"color":"white","playerId":"player-1"},)"
            R"({"color":"black","playerId":"player-2"}],)"
            R"("result":{"ending":"checkmate","winner":"player-1","winnerColor":"white"},)"
            R"("scoreSheet":[{"ending":"checkmate","winner":"player-1"}],)"
            R"("setupId":"kpk-opposition","setupName":"K+P vs K — Opposition",)"
            R"("variant":"kpk"}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"),
            R"({"update":{"gameEnded":{"result":{"ending":"checkmate","winner":"player-1",)"
            R"("winnerColor":"white"}}}})");
}

// The model's bounds are the decoder's to enforce: a clock outside them,
// or a move that is not four or five characters, is refused in band before
// the hub sees it. The table is seated at two, so the hub itself would
// start with any clock and take any move on turn: only the decoder refuses
// these, and the game does not start.
TEST_F(ChessWireTest, TheModelsBoundsAreRefusedInBand) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");
  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"createGame":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "roomState");
  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");
  ASSERT_TRUE(
      joiner->Send(CommandFrame("chess", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "roomState");

  const std::string initial_bounds =
      R"({"reason":"Value at '/chess/move/startGame/initialSeconds' failed to satisfy )"
      R"(constraint: Member must be between 30 and 1800, inclusive"})";
  for (const auto& [move, reason] : std::vector<std::pair<std::string, std::string>>{
           {R"({"move":{"startGame":{"initialSeconds":29}}})", initial_bounds},
           {R"({"move":{"startGame":{"initialSeconds":1801}}})", initial_bounds},
           {R"({"move":{"startGame":{"incrementSeconds":31}}})",
            R"({"reason":"Value at '/chess/move/startGame/incrementSeconds' failed to satisfy )"
            R"(constraint: Member must be between 0 and 30, inclusive"})"}}) {
    ASSERT_TRUE(creator->Send(CommandFrame("chess", move)).ok());
    EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"), reason) << move;
  }
  for (const auto& [setup_id, length] :
       std::vector<std::pair<std::string, int>>{{"", 0}, {std::string(33, 'a'), 33}}) {
    ASSERT_TRUE(creator
                    ->Send(CommandFrame(
                        "chess", R"({"move":{"startGame":{"setupId":")" + setup_id + R"("}}})"))
                    .ok());
    EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
              R"({"reason":"Value with length )" + std::to_string(length) +
                  R"( at '/chess/move/startGame/setupId' failed to satisfy constraint: Member )"
                  R"(must have length between 1 and 32, inclusive"})")
        << setup_id;
  }
  ASSERT_TRUE(
      creator->Send(CommandFrame("chess", R"({"move":{"startGame":{"setupId":"unknown"}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"unknown chess setup: unknown"})");
  // The control: the same start inside the bounds starts the game.
  ASSERT_TRUE(
      creator->Send(CommandFrame("chess", R"({"move":{"startGame":{"initialSeconds":30}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "chess"), R"({"update":{"gameStarted":{}}})");
  (void)EventPayload(NextFrame(*creator), "chess");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // A bot's strength is Stockfish's range, and the decoder holds it.
  for (const int elo : {1319, 3191}) {
    ASSERT_TRUE(
        creator->Send(CommandFrame("chess", R"({"move":{"addBot":{"elo":)" + std::to_string(elo) + "}}}"))
            .ok());
    EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
              R"({"reason":"Value at '/chess/move/addBot/elo' failed to satisfy )"
              R"(constraint: Member must be between 1320 and 3190, inclusive"})")
        << elo;
  }

  // On turn, a move of the wrong length never reaches the engine.
  for (const auto& [move, length] :
       std::vector<std::pair<std::string, int>>{{R"({"move":{"play":{"uci":"e7e"}}})", 3},
                                                {R"({"move":{"play":{"uci":"e7e8qq"}}})", 6}}) {
    ASSERT_TRUE(creator->Send(CommandFrame("chess", move)).ok());
    EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
              R"({"reason":"Value with length )" + std::to_string(length) +
                  R"( at '/chess/move/play/uci' failed to satisfy constraint: Member must have )"
                  R"(length between 4 and 5, inclusive"})")
        << move;
  }
  // The control: one of the right length that is no move is the engine's.
  ASSERT_TRUE(creator->Send(CommandFrame("chess", R"({"move":{"play":{"uci":"e7e6"}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"not a legal move: e7e6"})");
}

}  // namespace
}  // namespace games_hub
