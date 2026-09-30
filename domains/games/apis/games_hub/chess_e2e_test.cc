// Chess on the room stream, end to end through the generated client. The
// hub's clock and opening are fixed here: time moves only when a test
// moves it, and the position is the one the test names, so every legal
// move and every ending is known in advance.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace games_hub {
namespace {

using moonbase::games::ChessMove;
using moonbase::games::ChessUpdate;
using moonbase::games::GameCommands;

// White Kg6 Pe7 against Kh8: e7e8q (or e8r) is mate, e7e8b and e7e8n are not.
constexpr char kPromotionMates[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";
// White Ke1 Pe2 against Ke5: nothing ends soon.
constexpr char kQuiet[] = "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1";
constexpr int64_t kT0 = 1'800'000'000'000;

GameCommands Play(const std::string& uci) {
  moonbase::games::ChessPlay play;
  play.uci = uci;
  return Chess(ChessMove::FromPlay(play));
}

GameCommands Resign() { return Chess(ChessMove::FromResign(moonbase::games::ChessResign{})); }

class ChessFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    golf_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
    golf_->SetChessOpener([this] { return opening_; });
  }

  // A two-seat chess table, started with `start`: alice (seat 0) created it,
  // bob joined. Each seat has heard gameStarted and read its first view.
  struct Started {
    Table table;
    moonbase::games::ChessView view;
  };
  std::optional<Started> StartedTable(
      moonbase::games::ChessStartGame start = moonbase::games::ChessStartGame{}) {
    auto room = SeatedRoom(2);
    if (!room.has_value()) return std::nullopt;
    Table table{std::move(room->seats[0]), std::move(room->seats[1]), room->room_id, ""};
    if (!table.alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceiveChess(table.alice.stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join;
    join.gameId = table.game_id;
    if (!table.bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok()) return std::nullopt;
    if (!ReceiveChess(table.bob.stream, "gameJoined").has_value()) return std::nullopt;
    if (!table.alice.stream.Send(Chess(ChessMove::FromStartgame(start))).ok()) return std::nullopt;
    std::optional<moonbase::games::ChessView> view;
    for (Seat* seat : {&table.alice, &table.bob}) {
      if (!ReceiveChess(seat->stream, "gameStarted").has_value()) return std::nullopt;
      auto state = ReceiveChess(seat->stream, "gameState");
      if (!state.has_value()) return std::nullopt;
      view = state->as_gameState_or_null()->view;
    }
    return Started{std::move(table), *view};
  }

  std::optional<moonbase::games::ChessResult> Ended(Seat& seat) {
    auto ended = ReceiveChess(seat.stream, "gameEnded");
    if (!ended.has_value()) return std::nullopt;
    return ended->as_gameEnded_or_null()->result;
  }

  std::atomic<int64_t> now_ms_{kT0};
  chess_play::Opening opening_{kPromotionMates, 0};
};

TEST_F(ChessFixture, AStartedTableShowsBothSeatsThePositionTheColorsAndTheClock) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const auto& view = started->view;
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.variant, "kpk");
  EXPECT_EQ(view.fen, kPromotionMates);
  ASSERT_EQ(view.players.size(), 2u);
  EXPECT_EQ(view.players[0].playerId, started->table.alice.player_id);
  EXPECT_EQ(view.players[0].color, "white");
  EXPECT_EQ(view.players[1].color, "black");
  EXPECT_EQ(view.sideToMove, "white");
  EXPECT_EQ(view.currentPlayerId, started->table.alice.player_id);
  EXPECT_TRUE(view.moves.empty());
  EXPECT_FALSE(view.inCheck);
  EXPECT_NE(std::find(view.legalMoves.begin(), view.legalMoves.end(), "e7e8q"),
            view.legalMoves.end());
  ASSERT_TRUE(view.clock.has_value());
  // The default: three minutes and two seconds.
  EXPECT_EQ(view.clock->initialMs, 180'000);
  EXPECT_EQ(view.clock->incrementMs, 2'000);
  EXPECT_EQ(view.clock->whiteMs, 180'000);
  EXPECT_EQ(view.clock->blackMs, 180'000);
  EXPECT_FALSE(view.result.has_value());
}

TEST_F(ChessFixture, StartGameNamesTheClock) {
  moonbase::games::ChessStartGame start;
  start.initialSeconds = 60;
  start.incrementSeconds = 0;
  auto started = StartedTable(start);
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->view.clock.has_value());
  EXPECT_EQ(started->view.clock->initialMs, 60'000);
  EXPECT_EQ(started->view.clock->incrementMs, 0);
}

TEST_F(ChessFixture, TheWhitePieceIsWhicheverSeatTheOpeningSays) {
  opening_.white_seat = 1;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(started->view.players[0].color, "black");
  EXPECT_EQ(started->view.players[1].color, "white");
  EXPECT_EQ(started->view.currentPlayerId, started->table.bob.player_id);
}

TEST_F(ChessFixture, AMoveChargesTheClockAndHandsTheTurnOver) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  now_ms_ += 7'000;
  ASSERT_TRUE(table.alice.stream.Send(Play("e2e4")).ok());
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto view = AwaitChessView(
        seat->stream, [](const auto& v) { return !v.moves.empty(); }, "the first move");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->moves, std::vector<std::string>{"e2e4"});
    EXPECT_EQ(view->sideToMove, "black");
    EXPECT_EQ(view->currentPlayerId, table.bob.player_id);
    EXPECT_EQ(view->clock->whiteMs, 175'000);  // seven spent, two back
    EXPECT_EQ(view->clock->blackMs, 180'000);
    auto turn = ReceiveChess(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, table.bob.player_id);
  }
}

TEST_F(ChessFixture, OffTurnAndIllegalMovesAreRefusedInBand) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;

  ASSERT_TRUE(table.bob.stream.Send(Play("h8h7")).ok());
  auto off_turn = ReceiveCase(table.bob.stream, "commandRejected");
  ASSERT_TRUE(off_turn.has_value());
  EXPECT_EQ(off_turn->as_commandRejected_or_null()->reason, "not your turn");

  // A promotion must name its piece.
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8")).ok());
  auto unnamed = ReceiveCase(table.alice.stream, "commandRejected");
  ASSERT_TRUE(unnamed.has_value());
  EXPECT_EQ(unnamed->as_commandRejected_or_null()->reason, "not a legal move: e7e8");

  // The control: the same seat's legal move lands.
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8n")).ok());
  auto view = AwaitChessView(
      table.alice.stream, [](const auto& v) { return !v.moves.empty(); }, "the knight");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->moves, std::vector<std::string>{"e7e8n"});
}

TEST_F(ChessFixture, PromotingToMateEndsTheGameAndCreditsTheWinner) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto view =
        AwaitChessView(seat->stream, [](const auto& v) { return v.phase == "ended"; }, "the mate");
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->inCheck);
    EXPECT_TRUE(view->legalMoves.empty());
    EXPECT_FALSE(view->currentPlayerId.has_value());
    ASSERT_TRUE(view->result.has_value());
    auto result = Ended(*seat);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winner, table.alice.player_id);
    EXPECT_EQ(result->winnerColor, "white");
    EXPECT_EQ(result->ending, "checkmate");
  }
  // The room's stats moved with the finish.
  auto room = AwaitRoomState(
      table.alice.stream,
      [&](const moonbase::games::RoomState& state) {
        return std::any_of(state.players.begin(), state.players.end(),
                           [&](const auto& p) { return p.gamesWon == 1; });
      },
      "alice's win");
  ASSERT_TRUE(room.has_value());
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
    EXPECT_EQ(player.gamesWon, player.playerId == table.alice.player_id ? 1 : 0);
  }
}

TEST_F(ChessFixture, EitherSeatMayResignOffTurn) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.bob.stream.Send(Resign()).ok());
  auto result = Ended(table.alice);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.alice.player_id);
  EXPECT_EQ(result->ending, "resignation");
}

TEST_F(ChessFixture, TheSweepEndsAGameWhoseSideToMoveRanOutOfTime) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(ReceiveChess(table.bob.stream, "turnChanged").has_value());

  // Black's three minutes, less a millisecond: nothing to sweep.
  now_ms_ += 179'999;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  ExpectNoEvent(table.bob.stream);

  now_ms_ += 1;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 1);
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto result = Ended(*seat);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winner, table.alice.player_id);
    EXPECT_EQ(result->ending, "timeout");
  }
  // A finished game is nothing to sweep.
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
}

TEST_F(ChessFixture, ThePawnSideOutOfTimeOnlyDraws) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  now_ms_ += 180'000;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 1);
  auto result = Ended(started->table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->winner.has_value());
  EXPECT_FALSE(result->winnerColor.has_value());
  EXPECT_EQ(result->ending, "timeout");
}

TEST_F(ChessFixture, AMoveAfterTheFlagLosesOnTimeInsteadOfMoving) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(ReceiveChess(table.bob.stream, "turnChanged").has_value());
  now_ms_ += 200'000;
  ASSERT_TRUE(table.bob.stream.Send(Play("e5e6")).ok());
  auto view = AwaitChessView(
      table.bob.stream, [](const auto& v) { return v.phase == "ended"; }, "the flag");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->moves, std::vector<std::string>{"e2e4"});
  EXPECT_EQ(view->clock->blackMs, 0);
  auto result = Ended(table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.alice.player_id);
  EXPECT_EQ(result->ending, "timeout");
}

TEST_F(ChessFixture, ALeaverLosesByAbandonment) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(
      table.alice.stream.Send(Chess(ChessMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  auto result = Ended(table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.bob.player_id);
  EXPECT_EQ(result->winnerColor, "black");
  EXPECT_EQ(result->ending, "abandoned");
}

TEST_F(ChessFixture, AThirdSeatIsRefused) {
  auto room = SeatedRoom(3);
  ASSERT_TRUE(room.has_value());
  ASSERT_TRUE(room->seats[0]
                  .stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{})))
                  .ok());
  auto created = ReceiveChess(room->seats[0].stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(room->seats[1].stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveChess(room->seats[1].stream, "gameJoined").has_value());
  ASSERT_TRUE(room->seats[2].stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  auto refused = ReceiveCase(room->seats[2].stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game is full");
}

TEST_F(ChessFixture, AStartOnOneSeatIsRefused) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromStartgame(moonbase::games::ChessStartGame{}))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "need at least 2 players to start");
}

TEST_F(ChessFixture, AMoveBeforeTheStartIsRefused) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(Play("e7e8q")).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game not started");
  // Nor is a pending table anything to sweep, however late it is.
  now_ms_ += 10'000'000;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
}

// Without an opener set, the hub deals the variant's own: a random KPK.
class DefaultOpeningFixture : public GamesHubStreamFixture {};

TEST_F(DefaultOpeningFixture, TheHubDealsARandomKpk) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveChess(bob.stream, "gameJoined").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromStartgame(moonbase::games::ChessStartGame{}))).ok());
  auto view =
      AwaitChessView(alice.stream, [](const auto& v) { return v.phase == "playing"; }, "the deal");
  ASSERT_TRUE(view.has_value());
  const std::string placement = view->fen->substr(0, view->fen->find(' '));
  std::string pieces;
  std::copy_if(placement.begin(), placement.end(), std::back_inserter(pieces),
               [](char c) { return std::isalpha(static_cast<unsigned char>(c)); });
  std::sort(pieces.begin(), pieces.end());
  EXPECT_EQ(pieces, "KPk");
  EXPECT_EQ(view->sideToMove, "white");
}

}  // namespace
}  // namespace games_hub
