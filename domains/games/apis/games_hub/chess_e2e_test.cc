// Chess on the room stream, end to end through the generated client. The
// hub's clock and opening are fixed here: time moves only when a test
// moves it, and the position is the one the test names, so every legal
// move and every ending is known in advance.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/chess_cpp/pgn.h"
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

GameCommands Watch(const std::string& game_id) {
  moonbase::games::ChessWatch watch;
  watch.gameId = game_id;
  return Chess(ChessMove::FromWatch(watch));
}

GameCommands Challenge(std::optional<std::string> setup_id, std::optional<int> initial,
                       std::optional<int> increment) {
  moonbase::games::ChessStartGame challenge;
  challenge.setupId = std::move(setup_id);
  challenge.initialSeconds = initial;
  challenge.incrementSeconds = increment;
  return Chess(ChessMove::FromChallenge(challenge));
}

GameCommands LeaveTable() { return Chess(ChessMove::FromLeavegame(moonbase::games::LeaveGame{})); }

GameCommands History() {
  return Chess(ChessMove::FromHistory(moonbase::games::ChessHistoryRequest{}));
}

GameCommands Review(const std::string& game_id, int ordinal) {
  moonbase::games::ChessReviewRequest review;
  review.gameId = game_id;
  review.ordinal = ordinal;
  return Chess(ChessMove::FromReview(review));
}

GameCommands ReviewArchived(int64_t archive_id) {
  moonbase::games::ChessReviewRequest review;
  review.archiveId = archive_id;
  return Chess(ChessMove::FromReview(review));
}

GameCommands Publish(bool published) {
  moonbase::games::ChessPublish publish;
  publish.published = published;
  return Chess(ChessMove::FromPublish(publish));
}

class ChessFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    golf_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
    golf_->SetChessOpener(
        [this](std::string_view setup_id) -> absl::StatusOr<chess_play::ChessSetup> {
          if (setup_id == refused_setup_) return absl::UnavailableError("the opener is down");
          auto setup = chess_play::SelectChessSetup(setup_id, setup_gen_);
          if (setup.ok()) setup->opening = opening_;
          return setup;
        });
  }

  // A two-seat chess table, started with `start`: alice (seat 0) created it,
  // bob joined. Each seat has heard gameStarted and read its first view.
  // `idle` more members are in the room, at no table.
  struct Started {
    Table table;
    moonbase::games::ChessView view;
    std::vector<Seat> idle;
  };
  std::optional<Started> StartedTable(
      moonbase::games::ChessStartGame start = moonbase::games::ChessStartGame{}, int idle = 0) {
    auto room = SeatedRoom(2 + idle);
    if (!room.has_value()) return std::nullopt;
    Table table{std::move(room->seats[0]), std::move(room->seats[1]), room->room_id, ""};
    std::vector<Seat> idle_seats;
    for (int i = 0; i < idle; ++i) idle_seats.push_back(std::move(room->seats[2 + i]));
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
    if (!start.setupId.has_value()) start.setupId = std::string(chess_play::kRandomKpkSetup);
    if (!table.alice.stream.Send(Chess(ChessMove::FromStartgame(start))).ok()) return std::nullopt;
    std::optional<moonbase::games::ChessView> view;
    for (Seat* seat : {&table.alice, &table.bob}) {
      if (!ReceiveChess(seat->stream, "gameStarted").has_value()) return std::nullopt;
      auto state = ReceiveChess(seat->stream, "gameState");
      if (!state.has_value()) return std::nullopt;
      view = state->as_gameState_or_null()->view;
    }
    return Started{std::move(table), *view, std::move(idle_seats)};
  }

  // The room's record for each seat once `ready` holds for it: played
  // and won, keyed by player.
  std::map<std::string, std::pair<int, int>> RecordsOnce(
      Seat& seat, const std::function<bool(const moonbase::games::RoomState&)>& ready) {
    std::map<std::string, std::pair<int, int>> records;
    auto room = AwaitRoomState(seat.stream, ready, "the room's stats");
    if (!room.has_value()) return records;
    for (const auto& player : room->players) {
      records[player.playerId] = {player.gamesPlayed, player.gamesWon};
    }
    return records;
  }

  // The chess updates that reach `seat` before it goes quiet. A closed
  // stream is a failure, not silence.
  static std::vector<std::string> ChessHeard(Seat& seat) {
    std::vector<std::string> heard;
    while (true) {
      auto received = seat.stream.Receive(std::chrono::milliseconds(300));
      if (!received.ok()) break;
      if (!received->has_value()) {
        ADD_FAILURE() << seat.player_id << "'s stream closed";
        break;
      }
      if (const auto* chess = (*received)->as_chess_or_null()) {
        heard.emplace_back(chess->update.case_name());
      }
    }
    return heard;
  }
  static bool HearsChess(Seat& seat) { return !ChessHeard(seat).empty(); }

  std::optional<moonbase::games::ChessResult> Ended(Seat& seat) {
    auto ended = ReceiveChess(seat.stream, "gameEnded");
    if (!ended.has_value()) return std::nullopt;
    return ended->as_gameEnded_or_null()->result;
  }

  std::atomic<int64_t> now_ms_{kT0};
  // A setup the opener refuses, so a challenge's start can be made to fail.
  std::string refused_setup_;
  std::mt19937_64 setup_gen_{1234};
  chess_play::Opening opening_{kPromotionMates, 0};
};

TEST_F(ChessFixture, AStartedTableShowsBothSeatsThePositionTheColorsAndTheClock) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const auto& view = started->view;
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.variant, "kpk");
  EXPECT_EQ(view.setupId, "random-kpk");
  EXPECT_EQ(view.setupName, "Random K+P vs K");
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
  start.setupId = "kpk-opposition";
  start.initialSeconds = 60;
  start.incrementSeconds = 0;
  auto started = StartedTable(start);
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->view.clock.has_value());
  EXPECT_EQ(started->view.clock->initialMs, 60'000);
  EXPECT_EQ(started->view.clock->incrementMs, 0);
  EXPECT_EQ(started->view.setupId, "kpk-opposition");
  EXPECT_EQ(started->view.setupName, "K+P vs K — Opposition");
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
  // A draw is a game played for both, and a win for neither.
  const auto records = RecordsOnce(started->table.bob, [](const auto& room) {
    return std::all_of(room.players.begin(), room.players.end(),
                       [](const auto& p) { return p.gamesPlayed == 1; });
  });
  EXPECT_EQ(records.at(started->table.alice.player_id), std::make_pair(1, 0));
  EXPECT_EQ(records.at(started->table.bob.player_id), std::make_pair(1, 0));
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
  // The final view first: the table closed, the abandoned game on its sheet.
  auto closed = AwaitChessView(
      table.bob.stream, [](const auto& v) { return v.phase == "closed"; }, "the close");
  ASSERT_TRUE(closed.has_value());
  ASSERT_EQ(closed->scoreSheet.size(), 1u);
  EXPECT_EQ(closed->scoreSheet[0].winner, table.bob.player_id);
  EXPECT_EQ(closed->scoreSheet[0].ending, "abandoned");
  auto result = Ended(table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.bob.player_id);
  EXPECT_EQ(result->winnerColor, "black");
  EXPECT_EQ(result->ending, "abandoned");
  // The leaver played and lost.
  const auto records = RecordsOnce(table.bob, [&](const auto& room) {
    return std::any_of(room.players.begin(), room.players.end(),
                       [&](const auto& p) { return p.gamesWon == 1; });
  });
  EXPECT_EQ(records.at(table.bob.player_id), std::make_pair(1, 1));
  EXPECT_EQ(records.at(table.alice.player_id), std::make_pair(1, 0));
}

// The clock thread main starts: it flags without anyone sweeping by hand,
// a second start changes nothing, and the hub goes down with it running.
TEST_F(ChessFixture, TheClockThreadFlagsOnItsOwn) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  golf_->StartChessClocks(std::chrono::milliseconds(10));
  golf_->StartChessClocks(std::chrono::milliseconds(10));
  now_ms_ += 180'000;
  auto result = Ended(started->table.alice);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ending, "timeout");
}

// A table plays one game after another: a finished game stays on the
// table with its line on the sheet, and either seat starts the next, sides
// swapped, so the pawn goes back and forth.
TEST_F(ChessFixture, TheNextGameSwapsSidesAndKeepsTheScore) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto view =
        AwaitChessView(seat->stream, [](const auto& v) { return v.phase == "ended"; }, "the mate");
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(view->scoreSheet.size(), 1u);
    EXPECT_EQ(view->scoreSheet[0].winner, table.alice.player_id);
    EXPECT_EQ(view->scoreSheet[0].ending, "checkmate");
  }

  // Bob, the seat that lost, starts it.
  moonbase::games::ChessStartGame next;
  next.setupId = "qvr-basic";
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame(next))).ok());
  auto second = AwaitChessView(
      table.alice.stream, [](const auto& v) { return v.phase == "playing"; }, "the next game");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->players[1].playerId, table.bob.player_id);
  EXPECT_EQ(second->players[1].color, "white");
  EXPECT_EQ(second->currentPlayerId, table.bob.player_id);
  EXPECT_EQ(second->variant, "qvr");
  EXPECT_EQ(second->setupId, "qvr-basic");
  EXPECT_EQ(second->setupName, "Q vs R — Basic conversion");
  EXPECT_TRUE(second->moves.empty());
  EXPECT_FALSE(second->result.has_value());
  EXPECT_EQ(second->scoreSheet.size(), 1u);

  ASSERT_TRUE(table.bob.stream.Send(Play("e7e8q")).ok());
  auto scored = AwaitChessView(
      table.alice.stream, [](const auto& v) { return v.phase == "ended"; }, "the second mate");
  ASSERT_TRUE(scored.has_value());
  ASSERT_EQ(scored->scoreSheet.size(), 2u);
  EXPECT_EQ(scored->scoreSheet[1].winner, table.bob.player_id);
  auto result = Ended(table.alice);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.bob.player_id);
  EXPECT_EQ(result->winnerColor, "white");
  // Each game is the room's too: two played, one won apiece.
  const auto records = RecordsOnce(table.alice, [](const auto& room) {
    return std::all_of(room.players.begin(), room.players.end(),
                       [](const auto& p) { return p.gamesPlayed == 2; });
  });
  EXPECT_EQ(records.at(table.alice.player_id), std::make_pair(2, 1));
  EXPECT_EQ(records.at(table.bob.player_id), std::make_pair(2, 1));
}

TEST_F(ChessFixture, TheNextGameWaitsForThisOneToEnd) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  auto refused = ReceiveCase(started->table.bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "a game is in play");
}

// Between games a leave closes the table and scores nothing: the game
// before was scored as it ended, and nobody is told it again.
TEST_F(ChessFixture, LeavingBetweenGamesClosesTheTable) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  ASSERT_TRUE(Ended(table.bob).has_value());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(
      table.alice.stream.Send(Chess(ChessMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  bool closed = false;
  std::optional<moonbase::games::RoomState> room;
  while (true) {
    auto received = table.bob.stream.Receive(std::chrono::milliseconds(300));
    if (!received.ok() || !received->has_value()) break;
    if (const auto* chess = (*received)->as_chess_or_null()) {
      EXPECT_EQ(chess->update.as_gameEnded_or_null(), nullptr) << "the mate, told again";
      if (const auto* state = chess->update.as_gameState_or_null()) {
        closed = closed || state->view.phase == "closed";
        EXPECT_EQ(state->view.scoreSheet.size(), 1u);
      }
    }
    if (const auto* state = (*received)->as_roomState_or_null()) room = *state;
  }
  EXPECT_TRUE(closed);
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(room->games.empty());
  for (const auto& player : room->players) {
    EXPECT_EQ(player.gamesPlayed, 1) << player.playerId;
  }
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

// The rest of the room watches a table (#1633): the seats' own view,
// every move and the result, from a member who holds no seat.
TEST_F(ChessFixture, AWatcherSeesTheTableEveryMoveAndTheResult) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(table.game_id)).ok());
  auto watching = ReceiveChess(carol.stream, "gameState");
  ASSERT_TRUE(watching.has_value());
  const auto& view = watching->as_gameState_or_null()->view;
  EXPECT_EQ(view.gameId, table.game_id);
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.fen, kPromotionMates);
  ASSERT_EQ(view.players.size(), 2u);
  for (const auto& player : view.players) EXPECT_NE(player.playerId, carol.player_id);

  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  auto mated = AwaitChessView(
      carol.stream, [](const auto& v) { return v.phase == "ended"; }, "the mate, watched");
  ASSERT_TRUE(mated.has_value());
  EXPECT_EQ(mated->moves, std::vector<std::string>{"e7e8q"});
  auto result = Ended(carol);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.alice.player_id);
  EXPECT_EQ(result->ending, "checkmate");
}

// The view says whose turn it is: a watcher hears no turnChanged.
TEST_F(ChessFixture, AWatcherHearsViewsAndNoTurnChanged) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  // The control: the seats hear the turn change hands.
  ASSERT_TRUE(ReceiveChess(started->table.bob.stream, "turnChanged").has_value());
  EXPECT_EQ(ChessHeard(carol), std::vector<std::string>{"gameState"});
}

// A table that goes before it starts has no closed view to hand over:
// its watchers hear gameLeft.
TEST_F(ChessFixture, ATableGoneBeforeItStartsTellsItsWatchers) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& carol = room->seats[1];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string game_id = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(carol.stream.Send(Watch(game_id)).ok());
  ASSERT_TRUE(AwaitChessView(
                  carol.stream, [](const auto& v) { return v.phase == "waiting"; }, "watching")
                  .has_value());
  ASSERT_TRUE(alice.stream.Send(LeaveTable()).ok());
  auto left = ReceiveChess(carol.stream, "gameLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_gameLeft_or_null()->gameId, game_id);
  // Watching nothing now, so there is nothing to leave.
  ASSERT_TRUE(carol.stream.Send(LeaveTable()).ok());
  auto refused = ReceiveCase(carol.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a game");
}

// leaveGame stops a watch only in chess's own envelope.
TEST_F(ChessFixture, AnotherGamesLeaveDoesNotStopAChessWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(carol.stream
                  .Send(Castle(moonbase::games::CastleMove::FromLeavegame(
                      moonbase::games::LeaveGame{})))
                  .ok());
  auto refused = ReceiveCase(carol.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a game");
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  EXPECT_TRUE(AwaitChessView(
                  carol.stream, [](const auto& v) { return !v.moves.empty(); }, "still watching")
                  .has_value());
}

TEST_F(ChessFixture, WatchingAnIdWithANulIsRefused) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  ASSERT_TRUE(room->seats[0].stream.Send(Watch(std::string("GAME01\0alias", 12))).ok());
  auto refused = ReceiveCase(room->seats[0].stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "invalid game id");
}

TEST_F(ChessFixture, AWatcherSeesATableFromBeforeItStarts) {
  auto room = SeatedRoom(3);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  Seat& carol = room->seats[2];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string game_id = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(carol.stream.Send(Watch(game_id)).ok());
  auto waiting = AwaitChessView(
      carol.stream, [](const auto& v) { return v.phase == "waiting"; }, "the empty table");
  ASSERT_TRUE(waiting.has_value());
  EXPECT_EQ(waiting->players.size(), 1u);

  moonbase::games::JoinGame join;
  join.gameId = game_id;
  ASSERT_TRUE(bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  auto joined = AwaitChessView(
      carol.stream, [](const auto& v) { return v.players.size() == 2; }, "bob sits down");
  ASSERT_TRUE(joined.has_value());
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromStartgame(moonbase::games::ChessStartGame{}))).ok());
  EXPECT_TRUE(AwaitChessView(
                  carol.stream, [](const auto& v) { return v.phase == "playing"; }, "the start")
                  .has_value());
}

// A watcher holds no seat, so nothing it sends moves the table.
TEST_F(ChessFixture, AWatcherCannotMoveOrResign) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  for (const GameCommands& command : {Play("e7e8q"), Resign()}) {
    ASSERT_TRUE(carol.stream.Send(command).ok());
    auto refused = ReceiveCase(carol.stream, "commandRejected");
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a game");
  }
  EXPECT_FALSE(HearsChess(started->table.bob)) << "a watcher moved the table";
}

TEST_F(ChessFixture, ASeatCannotWatch) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(Watch(started->table.game_id)).ok());
  auto refused = ReceiveCase(started->table.bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "leave your current game first");
}

TEST_F(ChessFixture, WatchingATableThatIsNotThereIsRefused) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  ASSERT_TRUE(room->seats[0].stream.Send(Watch("NOPE")).ok());
  auto refused = ReceiveCase(room->seats[0].stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game not found");
}

TEST_F(ChessFixture, WatchingOutsideARoomIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());
  ASSERT_TRUE(seat->stream.Send(Watch("NOPE")).ok());
  auto refused = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a room");
}

TEST_F(ChessFixture, OnlyAChessTableCanBeWatched) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  ASSERT_TRUE(
      room->seats[0]
          .stream
          .Send(Move(moonbase::games::GolfMove::FromCreategame(moonbase::games::CreateGame{})))
          .ok());
  auto created = ReceiveGolf(room->seats[0].stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(
      room->seats[1].stream.Send(Watch(created->as_gameJoined_or_null()->view.gameId)).ok());
  auto refused = ReceiveCase(room->seats[1].stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays golf");
}

// leaveGame stops a watch the way it gets a seat up: gameLeft, then
// nothing more from that table.
TEST_F(ChessFixture, LeavingStopsTheWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(carol.stream.Send(LeaveTable()).ok());
  auto left = ReceiveChess(carol.stream, "gameLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_gameLeft_or_null()->gameId, started->table.game_id);

  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  // The control: the move went out.
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(carol));
}

TEST_F(ChessFixture, WatchingAnotherTableSwitchesToIt) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 2);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  Seat& dave = started->idle[1];
  ASSERT_TRUE(
      dave.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(dave.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string other = created->as_gameJoined_or_null()->view.gameId;

  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(AwaitChessView(
                  carol.stream, [&](const auto& v) { return v.gameId == started->table.game_id; },
                  "the first")
                  .has_value());
  ASSERT_TRUE(carol.stream.Send(Watch(other)).ok());
  ASSERT_TRUE(AwaitChessView(
                  carol.stream, [&](const auto& v) { return v.gameId == other; }, "the second")
                  .has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(carol)) << "the first table, still watched";
}

// Sitting down anywhere ends a watch: a seat hears its own table only.
TEST_F(ChessFixture, OpeningATableStopsTheWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(
      carol.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameJoined").has_value());
  HearsChess(carol);  // the room's word of carol's table
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(carol));
}

TEST_F(ChessFixture, JoiningATableStopsTheWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 2);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  Seat& dave = started->idle[1];
  ASSERT_TRUE(
      dave.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(dave.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(AwaitChessView(
                  carol.stream, [&](const auto& v) { return v.gameId == started->table.game_id; },
                  "watching")
                  .has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(carol.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameJoined").has_value());
  HearsChess(carol);
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(carol));
}

TEST_F(ChessFixture, LeavingTheRoomStopsTheWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(carol.stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(carol.stream, "roomLeft").has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(carol));
}

// Watching is presence, like the world: a resumed session watches nothing
// until it asks again.
TEST_F(ChessFixture, AResumeDoesNotRestoreTheWatch) {
  opening_.fen = kQuiet;
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  const std::string token = carol.resume_token;
  carol.stream.Close();
  // The room hears carol go before she comes back.
  ASSERT_TRUE(AwaitRoomState(
                  started->table.alice.stream,
                  [&](const auto& room) {
                    return std::any_of(room.players.begin(), room.players.end(),
                                       [&](const auto& p) {
                                         return p.playerId == carol.player_id && !p.connected;
                                       });
                  },
                  "carol parked")
                  .has_value());
  auto resumed = OpenSeat(token);
  ASSERT_TRUE(resumed.has_value());
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  ASSERT_TRUE(ready->as_sessionReady_or_null()->resumed);
  HearsChess(*resumed);  // the resync
  ASSERT_TRUE(started->table.alice.stream.Send(Play("e2e4")).ok());
  ASSERT_TRUE(
      AwaitChessView(
          started->table.bob.stream, [](const auto& v) { return !v.moves.empty(); }, "the move")
          .has_value());
  EXPECT_FALSE(HearsChess(*resumed));
}

// The close a leaving seat causes is the watcher's last word from the
// table: the final view and the result.
TEST_F(ChessFixture, AWatcherSeesTheTableClose) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(Watch(started->table.game_id)).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameState").has_value());
  ASSERT_TRUE(started->table.alice.stream.Send(LeaveTable()).ok());
  auto closed =
      AwaitChessView(carol.stream, [](const auto& v) { return v.phase == "closed"; }, "the close");
  ASSERT_TRUE(closed.has_value());
  auto result = Ended(carol);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ending, "abandoned");
}

// A table's one seat posts its terms (#1633): the room's list says them,
// and the view carries them, before anyone joins.
TEST_F(ChessFixture, APostedChallengeShowsInTheRoomAndTheView) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  EXPECT_FALSE(created->as_gameJoined_or_null()->view.terms.has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge("kpk-opposition", 60, 1)).ok());
  auto posted =
      AwaitChessView(alice.stream, [](const auto& v) { return v.terms.has_value(); }, "the terms");
  ASSERT_TRUE(posted.has_value());
  EXPECT_EQ(posted->phase, "waiting");
  EXPECT_EQ(posted->terms->setupId, "kpk-opposition");
  EXPECT_EQ(posted->terms->setupName, "K+P vs K — Opposition");
  EXPECT_EQ(posted->terms->initialSeconds, 60);
  EXPECT_EQ(posted->terms->incrementSeconds, 1);
  auto listed = AwaitRoomState(
      bob.stream, [](const auto& r) { return !r.games.empty() && r.games[0].terms.has_value(); },
      "the challenge in the room's list");
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(listed->games[0].terms, "K+P vs K — Opposition · 1+1");
}

TEST_F(ChessFixture, JoiningAChallengeStartsItOnItsTerms) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge("kpk-opposition", 60, 1)).ok());
  ASSERT_TRUE(AwaitChessView(
                  alice.stream, [](const auto& v) { return v.terms.has_value(); }, "the terms")
                  .has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(ReceiveChess(seat->stream, "gameStarted").has_value()) << seat->player_id;
    auto playing = AwaitChessView(
        seat->stream, [](const auto& v) { return v.phase == "playing"; }, "the start");
    ASSERT_TRUE(playing.has_value());
    EXPECT_EQ(playing->setupId, "kpk-opposition");
    ASSERT_TRUE(playing->clock.has_value());
    EXPECT_EQ(playing->clock->initialMs, 60'000);
    EXPECT_EQ(playing->clock->incrementMs, 1'000);
    EXPECT_FALSE(playing->terms.has_value());
  }
  // In play, the room's list names no terms.
  auto listed = AwaitRoomState(
      bob.stream, [](const auto& r) { return !r.games.empty() && r.games[0].status != "waiting"; },
      "in play");
  ASSERT_TRUE(listed.has_value());
  EXPECT_FALSE(listed->games[0].terms.has_value());
}

// The control for the challenge's start: a table with no terms waits for
// its seats to choose, as it always has.
TEST_F(ChessFixture, JoiningATableWithNoTermsWaitsForTheStart) {
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
  auto joined = ReceiveChess(bob.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_gameJoined_or_null()->view.phase, "waiting");
  for (const std::string& heard : ChessHeard(bob)) EXPECT_NE(heard, "gameStarted");
}

TEST_F(ChessFixture, AChallengeTakesStartGamesDefaultsForWhatItLeavesOut) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge(std::nullopt, std::nullopt, std::nullopt)).ok());
  auto posted =
      AwaitChessView(alice.stream, [](const auto& v) { return v.terms.has_value(); }, "the terms");
  ASSERT_TRUE(posted.has_value());
  EXPECT_EQ(posted->terms->setupId, "standard");
  EXPECT_EQ(posted->terms->initialSeconds, 180);
  EXPECT_EQ(posted->terms->incrementSeconds, 2);
  // Posting again replaces them.
  ASSERT_TRUE(alice.stream.Send(Challenge("qvr-basic", 300, 3)).ok());
  auto replaced = AwaitChessView(
      alice.stream,
      [](const auto& v) { return v.terms.has_value() && v.terms->setupId != "standard"; },
      "the new terms");
  ASSERT_TRUE(replaced.has_value());
  EXPECT_EQ(replaced->terms->setupId, "qvr-basic");
  EXPECT_EQ(replaced->terms->initialSeconds, 300);
}

// The room's line gives a clock of whole minutes in minutes, and any other
// in seconds.
TEST_F(ChessFixture, AChallengesLineGivesOddSecondsAsSeconds) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge("standard", 90, 0)).ok());
  auto listed = AwaitRoomState(
      alice.stream, [](const auto& r) { return !r.games.empty() && r.games[0].terms.has_value(); },
      "the challenge in the room's list");
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(listed->games[0].terms, "Standard starting position · 90s+0");
}

// A start that fails leaves the table full and waiting. Terms are the
// poster's: once anyone leaves, the table has none, and the next seat to
// fill it chooses at the start like any other.
TEST_F(ChessFixture, AFailedStartTellsTheJoinerAndALeaveClearsTheTerms) {
  refused_setup_ = "qvr-basic";
  auto room = SeatedRoom(3);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  Seat& carol = room->seats[2];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge("qvr-basic", 60, 0)).ok());
  ASSERT_TRUE(AwaitChessView(
                  alice.stream, [](const auto& v) { return v.terms.has_value(); }, "the terms")
                  .has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  auto refused = ReceiveCase(bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "the opener is down");

  ASSERT_TRUE(alice.stream.Send(LeaveTable()).ok());
  auto alone =
      AwaitChessView(bob.stream, [](const auto& v) { return v.players.size() == 1; }, "alice gone");
  ASSERT_TRUE(alone.has_value());
  EXPECT_FALSE(alone->terms.has_value());
  auto listed = AwaitRoomState(
      carol.stream, [](const auto& r) { return !r.games.empty() && r.games[0].playerCount == 1; },
      "bob alone");
  ASSERT_TRUE(listed.has_value());
  EXPECT_FALSE(listed->games[0].terms.has_value());
  // And carol's join waits for the start rather than taking alice's terms.
  ASSERT_TRUE(carol.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveChess(carol.stream, "gameJoined").has_value());
  for (const std::string& heard : ChessHeard(carol)) EXPECT_NE(heard, "gameStarted");
}

TEST_F(ChessFixture, AChallengeNamesASetupTheHubHas) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveChess(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge("nope", 60, 0)).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "unknown chess setup: nope");
}

// Terms are posted to a table waiting on its second seat, by its one seat.
TEST_F(ChessFixture, OnlyATablesLoneSeatPostsAChallenge) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  // At no table.
  ASSERT_TRUE(alice.stream.Send(Challenge(std::nullopt, std::nullopt, std::nullopt)).ok());
  auto nowhere = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(nowhere.has_value());
  EXPECT_EQ(nowhere->as_commandRejected_or_null()->reason, "not in a game");
  // Two seats, no terms: they choose at the start.
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveChess(alice.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinGame join;
  join.gameId = created->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(bob.stream.Send(Chess(ChessMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveChess(bob.stream, "gameJoined").has_value());
  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(seat->stream.Send(Challenge(std::nullopt, std::nullopt, std::nullopt)).ok());
    auto full = ReceiveCase(seat->stream, "commandRejected");
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->as_commandRejected_or_null()->reason, "the table is full");
  }
  // Started.
  ASSERT_TRUE(
      alice.stream.Send(Chess(ChessMove::FromStartgame(moonbase::games::ChessStartGame{}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  alice.stream, [](const auto& v) { return v.phase == "playing"; }, "the start")
                  .has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge(std::nullopt, std::nullopt, std::nullopt)).ok());
  auto started = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(started.has_value());
  EXPECT_EQ(started->as_commandRejected_or_null()->reason, "game already started");
}

TEST_F(ChessFixture, OnlyAChessTableTakesAChallenge) {
  auto room = SeatedRoom(1);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  ASSERT_TRUE(
      alice.stream
          .Send(Move(moonbase::games::GolfMove::FromCreategame(moonbase::games::CreateGame{})))
          .ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(alice.stream.Send(Challenge(std::nullopt, std::nullopt, std::nullopt)).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "that table plays golf");
}

// A chess game in the stats archive (#1571): started as chess at its two
// seats, finished once, by the commit that ended it.
class ChessEventFixture : public ChessFixture {
 protected:
  void SetUp() override {
    ChessFixture::SetUp();
    golf_->SetEventWriter(
        [this](absl::Time, std::string_view line) { events_.emplace_back(line); });
  }
  // The writer goes first: the hub's teardown can end a game, and
  // `events_` dies before the base's hub does.
  void TearDown() override {
    if (golf_ != nullptr) golf_->SetEventWriter(nullptr);
    ChessFixture::TearDown();
  }
  std::vector<std::string> events_;
};

TEST_F(ChessEventFixture, AChessGameIsRecordedStartedAndFinishedAsChess) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(started->table.alice).has_value());
  std::vector<std::string> games;
  for (const std::string& line : events_) {
    if (line.find(R"("event":"game_)") != std::string::npos) games.push_back(line);
  }
  ASSERT_EQ(games.size(), 2u);
  EXPECT_THAT(games[0], ::testing::HasSubstr(R"("event":"game_started")"));
  EXPECT_THAT(games[0], ::testing::HasSubstr(R"("variant":"chess")"));
  EXPECT_THAT(games[0], ::testing::HasSubstr(R"("players":2)"));
  EXPECT_THAT(games[1], ::testing::HasSubstr(R"("event":"game_finished")"));
  EXPECT_THAT(games[1], ::testing::HasSubstr(R"("variant":"chess")"));
  EXPECT_THAT(games[1], ::testing::HasSubstr(R"("outcome":"completed")"));
}

// A store that cannot take a game's finish while `down` holds, counting
// every finish it was asked for.
class FinishOutageStore final : public HubStore {
 public:
  void Enqueue(std::vector<Op> ops) override { delegate_.Enqueue(std::move(ops)); }
  void Flush() override { delegate_.Flush(); }
  absl::StatusOr<Snapshot> LoadSnapshot() override { return delegate_.LoadSnapshot(); }
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override {
    return delegate_.CommitGameSave(row, notify_payload);
  }
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& deltas,
                                        const std::string& notify_payload) override {
    ++finishes;
    if (down) return absl::UnavailableError("hub store unreachable");
    return delegate_.CommitGameFinish(row, deltas, notify_payload);
  }
  absl::StatusOr<bool> CommitChessEvent(const ChessEventRow& row,
                                        const std::string& notify_payload) override {
    return delegate_.CommitChessEvent(row, notify_payload);
  }
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override {
    return delegate_.LoadGame(room_id, game_id);
  }
  absl::StatusOr<ChessHistory> LoadChessHistory(const std::string& room_id, int limit) override {
    return delegate_.LoadChessHistory(room_id, limit);
  }
  absl::StatusOr<std::optional<ChessGameRow>> LoadChessGame(const std::string& room_id,
                                                            const ChessGameKey& key) override {
    return delegate_.LoadChessGame(room_id, key);
  }
  absl::StatusOr<std::vector<PublishedChessGame>> LoadPublishedChess(int64_t after_archive_id,
                                                                     int limit) override {
    return delegate_.LoadPublishedChess(after_archive_id, limit);
  }
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override {
    return delegate_.LoadRoom(room_id);
  }

  std::atomic<bool> down{false};
  std::atomic<int> finishes{0};

 private:
  MemoryHubStore delegate_;
};

class ChessOutageFixture : public ChessFixture {
 protected:
  std::shared_ptr<HubStore> MakeStore() override {
    outage_ = std::make_shared<FinishOutageStore>();
    return outage_;
  }
  std::shared_ptr<FinishOutageStore> outage_;
};

// A flag the store cannot take is tried again after kChessFlagRetry, not
// on every tick: during an outage the sweep does not hammer the store
// with the hub's lock held.
TEST_F(ChessOutageFixture, AFlagTheStoreCannotTakeWaitsBeforeItsRetry) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  now_ms_ += 180'000;
  outage_->down = true;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(outage_->finishes, 1);
  now_ms_ += GolfHub::kChessFlagRetry.count() - 1;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(outage_->finishes, 1);
  now_ms_ += 1;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(outage_->finishes, 2);
  // Back up: the next retry lands the flag.
  outage_->down = false;
  now_ms_ += GolfHub::kChessFlagRetry.count();
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 1);
  auto result = Ended(started->table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ending, "timeout");
}

// A game that ends while its flag is backing off takes its retry with it:
// the map holds only games still waiting on the store.
TEST_F(ChessOutageFixture, AGameThatEndsWhileItsFlagWaitsLeavesNoRetryBehind) {
  opening_.fen = kQuiet;
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  now_ms_ += 180'000;
  outage_->down = true;
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(golf_->ChessFlagRetriesPending(), 1u);
  // The control: another sweep inside the retry window keeps it.
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(golf_->ChessFlagRetriesPending(), 1u);
  // The store comes back and the game ends another way.
  outage_->down = false;
  ASSERT_TRUE(started->table.bob.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(started->table.alice).has_value());
  EXPECT_EQ(golf_->SweepChessClocksOnce(), 0);
  EXPECT_EQ(golf_->ChessFlagRetriesPending(), 0u);
}

// Without a setup named, the hub starts ordinary chess.
class DefaultOpeningFixture : public GamesHubStreamFixture {};

TEST_F(DefaultOpeningFixture, TheHubDefaultsToTheStandardStartingPosition) {
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
  EXPECT_EQ(view->fen,
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  EXPECT_EQ(view->sideToMove, "white");
  EXPECT_EQ(view->variant, "standard");
  EXPECT_EQ(view->setupId, "standard");
  EXPECT_EQ(view->setupName, "Standard starting position");
  EXPECT_EQ(view->defaultSetupId, "standard");
}

TEST_F(DefaultOpeningFixture, TheHubDealsASelectedPracticeSetup) {
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
  moonbase::games::ChessStartGame start;
  start.setupId = "rpr-lucena";
  ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromStartgame(start))).ok());
  auto view =
      AwaitChessView(alice.stream, [](const auto& v) { return v.phase == "playing"; }, "the deal");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->variant, "rpr");
  EXPECT_EQ(view->setupId, "rpr-lucena");
  EXPECT_EQ(view->setupName, "R+P vs R — Lucena position");
  EXPECT_EQ(view->fen, "1K1R4/1P6/1k6/8/8/8/r7/8 w - - 0 1");
}

TEST_F(DefaultOpeningFixture, TheNextGameCanSelectAnotherCatalogPosition) {
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

  moonbase::games::ChessStartGame first;
  first.setupId = "kpk-opposition";
  ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromStartgame(first))).ok());
  ASSERT_TRUE(AwaitChessView(
                  alice.stream,
                  [](const auto& view) {
                    return view.phase == "playing" && view.setupId == "kpk-opposition";
                  },
                  "the first setup")
                  .has_value());
  ASSERT_TRUE(alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(AwaitChessView(
                  alice.stream, [](const auto& view) { return view.phase == "ended"; }, "the end")
                  .has_value());

  moonbase::games::ChessStartGame next;
  next.setupId = "qvr-basic";
  ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromStartgame(next))).ok());
  auto view = AwaitChessView(
      alice.stream,
      [](const auto& candidate) {
        return candidate.phase == "playing" && candidate.setupId == "qvr-basic";
      },
      "the next setup");
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->variant, "qvr");
  EXPECT_EQ(view->setupName, "Q vs R — Basic conversion");
  EXPECT_EQ(view->fen, "4k3/8/8/8/8/8/1r6/3QK3 w - - 0 1");
}

// A room keeps its finished games (#1637): any member asks, seated or
// not, and hears them newest first, each with its sides and result.
TEST_F(ChessFixture, AFinishedGameIsInTheRoomsHistory) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(History()).ok());
  auto empty = ReceiveChess(carol.stream, "history");
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty->as_history_or_null()->games.empty()) << "a game in play is not history";
  EXPECT_FALSE(empty->as_history_or_null()->published);

  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(carol.stream.Send(History()).ok());
  auto history = ReceiveChess(carol.stream, "history");
  ASSERT_TRUE(history.has_value());
  const auto& games = history->as_history_or_null()->games;
  ASSERT_EQ(games.size(), 1u);
  EXPECT_EQ(games[0].gameId, table.game_id);
  EXPECT_EQ(games[0].ordinal, 1);
  EXPECT_EQ(games[0].white, table.alice.player_id);
  EXPECT_EQ(games[0].black, table.bob.player_id);
  EXPECT_EQ(games[0].result.winner, table.alice.player_id);
  EXPECT_EQ(games[0].result.winnerColor, "white");
  EXPECT_EQ(games[0].result.ending, "checkmate");
  EXPECT_EQ(games[0].setupId, "random-kpk");
  EXPECT_EQ(games[0].setupName, "Random K+P vs K");
  EXPECT_EQ(games[0].plies, 1);
  EXPECT_GT(games[0].endedAtMs, 0);
  EXPECT_GT(games[0].archiveId, 0);
  EXPECT_FALSE(games[0].published);

  // The table's next game is the second line, and heads the history.
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  table.bob.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the second game")
                  .has_value());
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  history = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->as_history_or_null()->games.size(), 2u);
  EXPECT_EQ(history->as_history_or_null()->games[0].ordinal, 2);
  EXPECT_EQ(history->as_history_or_null()->games[0].result.ending, "resignation");
}

// A game ended by a seat leaving is archived as any ending is: the
// archive keys on the game being over, not on how.
TEST_F(ChessFixture, AGameEndedByALeaveIsInTheHistory) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(LeaveTable()).ok());
  ASSERT_TRUE(ReceiveChess(table.bob.stream, "gameEnded").has_value());
  Seat& carol = started->idle[0];
  ASSERT_TRUE(carol.stream.Send(History()).ok());
  auto history = ReceiveChess(carol.stream, "history");
  ASSERT_TRUE(history.has_value());
  const auto& games = history->as_history_or_null()->games;
  ASSERT_EQ(games.size(), 1u);
  EXPECT_EQ(games[0].result.ending, "abandoned");
  EXPECT_EQ(games[0].result.winner, table.bob.player_id);
}

TEST_F(ChessFixture, AReviewIsTheGameMoveByMoveAndItsPgn) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  ASSERT_TRUE(Ended(table.bob).has_value());
  ASSERT_TRUE(table.bob.stream.Send(Review(table.game_id, 1)).ok());
  auto review = ReceiveChess(table.bob.stream, "review");
  ASSERT_TRUE(review.has_value());
  const auto& game = *review->as_review_or_null();
  EXPECT_EQ(game.summary.gameId, table.game_id);
  EXPECT_EQ(game.summary.ordinal, 1);
  EXPECT_EQ(game.moves, std::vector<std::string>{"e7e8q"});
  EXPECT_EQ(game.san, std::vector<std::string>{"e8=Q#"});
  ASSERT_EQ(game.fens.size(), 2u);
  EXPECT_EQ(game.fens[0], kPromotionMates);
  EXPECT_EQ(game.fens[1], "4Q2k/8/6K1/8/8/8/8/8 b - - 0 1");
  EXPECT_THAT(game.pgn, ::testing::HasSubstr("[White \"" + table.alice.player_id + "\"]"));
  EXPECT_THAT(game.pgn, ::testing::HasSubstr("[Site \"https://muchq.com/games/chess/" +
                                             std::to_string(game.summary.archiveId) + "\"]"));
  EXPECT_THAT(game.pgn, ::testing::Not(::testing::HasSubstr(table.room_id)))
      << "the PGN names no room: a room code is all it takes to join one";
  EXPECT_THAT(game.pgn, ::testing::HasSubstr("[SetUp \"1\"]"));
  EXPECT_THAT(game.pgn, ::testing::HasSubstr("1. e8=Q# 1-0\n"));
}

// A table code minted again shares its lines with the old table's games;
// the id names either, the table and line the newest.
TEST_F(ChessFixture, AReviewNamesAGameByItsArchiveId) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  auto history = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(history.has_value());
  const int64_t archive_id = history->as_history_or_null()->games.at(0).archiveId;
  ASSERT_TRUE(table.alice.stream.Send(ReviewArchived(archive_id)).ok());
  auto review = ReceiveChess(table.alice.stream, "review");
  ASSERT_TRUE(review.has_value());
  EXPECT_EQ(review->as_review_or_null()->summary.archiveId, archive_id);
  EXPECT_EQ(review->as_review_or_null()->moves, std::vector<std::string>{"e7e8q"});
}

TEST_F(ChessFixture, AReviewNamingTheGameBothWaysOrHalfAWayIsRefused) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Seat& alice = started->table.alice;
  moonbase::games::ChessReviewRequest both;
  both.archiveId = 1;
  both.gameId = started->table.game_id;
  both.ordinal = 1;
  moonbase::games::ChessReviewRequest half;
  half.gameId = started->table.game_id;
  for (const auto& request : {both, half, moonbase::games::ChessReviewRequest{}}) {
    ASSERT_TRUE(alice.stream.Send(Chess(ChessMove::FromReview(request))).ok());
    auto refused = ReceiveCase(alice.stream, "commandRejected");
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->as_commandRejected_or_null()->reason,
              "name a game by archiveId, or by gameId and ordinal");
  }
}

TEST_F(ChessFixture, AReviewOfNoSuchGameIsRefused) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Review(table.game_id, 1)).ok());
  auto refused = ReceiveCase(table.alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no such game in this room");
}

// Another room's games are not this room's history, nor reviewable here.
TEST_F(ChessFixture, ARoomsHistoryIsItsOwn) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play("e7e8q")).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  auto elsewhere = SeatedRoom(1);
  ASSERT_TRUE(elsewhere.has_value());
  Seat& dave = elsewhere->seats[0];
  ASSERT_TRUE(dave.stream.Send(History()).ok());
  auto history = ReceiveChess(dave.stream, "history");
  ASSERT_TRUE(history.has_value());
  EXPECT_TRUE(history->as_history_or_null()->games.empty());
  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  auto theirs = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(theirs.has_value());
  ASSERT_TRUE(
      dave.stream.Send(ReviewArchived(theirs->as_history_or_null()->games.at(0).archiveId)).ok());
  auto by_id = ReceiveCase(dave.stream, "commandRejected");
  ASSERT_TRUE(by_id.has_value());
  EXPECT_EQ(by_id->as_commandRejected_or_null()->reason, "no such game in this room");
  ASSERT_TRUE(dave.stream.Send(Review(table.game_id, 1)).ok());
  auto refused = ReceiveCase(dave.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "no such game in this room");
}

TEST_F(ChessFixture, HistoryOutsideARoomIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());
  ASSERT_TRUE(seat->stream.Send(History()).ok());
  auto refused = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a room");
}

// Publishing is the room's to know: every member hears who did it, and
// each game remembers whether the room was published when it ended.
TEST_F(ChessFixture, PublishingTellsTheRoomAndMarksTheGamesThatEndAfter) {
  auto started = StartedTable({}, 1);
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(started->idle[0].stream.Send(Publish(true)).ok());
  for (Seat* seat : {&table.alice, &table.bob, &started->idle[0]}) {
    auto published = ReceiveChess(seat->stream, "published");
    ASSERT_TRUE(published.has_value()) << seat->player_id;
    EXPECT_TRUE(published->as_published_or_null()->published);
    EXPECT_EQ(published->as_published_or_null()->by, started->idle[0].player_id);
  }
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  table.alice.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the second game")
                  .has_value());
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());

  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  auto history = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(history.has_value());
  EXPECT_TRUE(history->as_history_or_null()->published);
  const auto& games = history->as_history_or_null()->games;
  ASSERT_EQ(games.size(), 2u);
  EXPECT_TRUE(games[0].published);
  EXPECT_FALSE(games[1].published) << "ended before the room published";

  ASSERT_TRUE(table.bob.stream.Send(Publish(false)).ok());
  auto withdrawn = ReceiveChess(started->idle[0].stream, "published");
  ASSERT_TRUE(withdrawn.has_value());
  EXPECT_FALSE(withdrawn->as_published_or_null()->published);
  EXPECT_EQ(withdrawn->as_published_or_null()->by, table.bob.player_id);
}

TEST_F(ChessFixture, PublishingOutsideARoomIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());
  ASSERT_TRUE(seat->stream.Send(Publish(true)).ok());
  auto refused = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not in a room");
}

// The public feed: games that ended in a published room, over plain HTTP
// through the generated client, in archive order, read on with `after`;
// one stays there after the room stops publishing, and the indexer's
// stream reader takes the page as that many games, each its own Site.
TEST_F(ChessFixture, ThePublicFeedServesGamesThatEndedPublished) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  const auto next_game = [&] {
    ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
    ASSERT_TRUE(AwaitChessView(
                    table.alice.stream, [](const auto& view) { return view.phase == "playing"; },
                    "the next game")
                    .has_value());
  };
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());  // private
  ASSERT_TRUE(Ended(table.alice).has_value());
  auto empty = client_->ExportChessGames({});
  ASSERT_TRUE(empty.ok()) << empty.error().message();
  EXPECT_EQ(empty->contentType, "application/x-chess-pgn");
  EXPECT_TRUE(empty->pgn.empty()) << "a game that ended private is not public";

  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  for (int game = 0; game < 2; ++game) {
    next_game();
    ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
    ASSERT_TRUE(Ended(table.alice).has_value());
  }
  ASSERT_TRUE(table.alice.stream.Send(Publish(false)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());

  auto exported = client_->ExportChessGames({});
  ASSERT_TRUE(exported.ok()) << exported.error().message();
  const std::string pgn = exported->pgn.ToString();
  EXPECT_THAT(pgn, ::testing::Not(::testing::HasSubstr(table.room_id)));
  std::istringstream stream(pgn);
  std::vector<std::string> sites;
  ASSERT_TRUE(chess_cpp::ParseGames(stream, [&](chess_cpp::ParsedGame game) {
                sites.emplace_back(game.headers.Get("Site").value_or(""));
                EXPECT_TRUE(game.headers.Get("UTCDate").has_value());
                EXPECT_TRUE(game.headers.Get("UTCTime").has_value());
                return absl::OkStatus();
              }).ok());
  ASSERT_EQ(sites.size(), 2u) << "the two that ended published, still out after withdrawing";
  EXPECT_NE(sites[0], sites[1]);

  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  auto history = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(history.has_value());
  const auto& games = history->as_history_or_null()->games;
  ASSERT_EQ(games.size(), 3u);
  EXPECT_EQ(sites[0], "https://muchq.com/games/chess/" + std::to_string(games[1].archiveId))
      << "archive order";
  moonbase::games::ExportChessGamesInput after;
  after.after = games[1].archiveId;
  auto rest = client_->ExportChessGames(after);
  ASSERT_TRUE(rest.ok());
  EXPECT_THAT(rest->pgn.ToString(),
              ::testing::HasSubstr("/games/chess/" + std::to_string(games[0].archiveId) + "\""));
  EXPECT_THAT(rest->pgn.ToString(),
              ::testing::Not(::testing::HasSubstr("/games/chess/" +
                                                  std::to_string(games[1].archiveId) + "\"")));
}

// A game that ends while the room is published asks 1d4 to index each of
// its players for the month it ended in, so it reaches 1d4 without anyone
// asking; one that ends private asks nothing.
TEST_F(ChessFixture, AGameEndingPublishedAsksToIndexItsPlayers) {
  std::mutex mu;
  std::vector<std::pair<std::string, std::string>> asked;
  golf_->SetChessIndexer([&](const one_d4::IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    asked.emplace_back(ask.player_id, ask.month);
  });
  const auto seen = [&] {
    const std::lock_guard<std::mutex> lock(mu);
    return asked;
  };
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  EXPECT_TRUE(seen().empty()) << "a game that ended private";

  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  table.alice.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the next game")
                  .has_value());
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  // kT0 is 2027-01-15, UTC.
  EXPECT_THAT(seen(), ::testing::UnorderedElementsAre(std::pair{table.alice.player_id, "2027-01"},
                                                      std::pair{table.bob.player_id, "2027-01"}));
}

// A game a player ends by leaving is in the feed with both of them, so
// both are asked for, the one who left included.
TEST_F(ChessFixture, AGameEndedByALeaveAsksToIndexWhoeverLeftToo) {
  std::mutex mu;
  std::vector<std::string> asked;
  golf_->SetChessIndexer([&](const one_d4::IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    asked.push_back(ask.player_id);
  });
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  const std::lock_guard<std::mutex> lock(mu);
  EXPECT_THAT(asked, ::testing::UnorderedElementsAre(table.alice.player_id, table.bob.player_id));
}

// A game ends in the database's clock and is asked for in the hub's, so
// one that ends in the first minute of a month is asked for in the month
// before too, in case it ended there.
TEST_F(ChessFixture, AGameEndingInAMonthsFirstMinuteAsksForTheMonthBeforeToo) {
  constexpr int64_t kFebruary = 1'801'440'000'000;  // 2027-02-01T00:00:00Z
  std::mutex mu;
  std::vector<std::pair<std::string, std::string>> asked;
  golf_->SetChessIndexer([&](const one_d4::IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    asked.emplace_back(ask.player_id, ask.month);
  });
  const auto seen = [&] {
    const std::lock_guard<std::mutex> lock(mu);
    return asked;
  };
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  const std::string& alice = table.alice.player_id;
  const std::string& bob = table.bob.player_id;
  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  now_ms_ = kFebruary + 59'999;
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  EXPECT_THAT(seen(), ::testing::UnorderedElementsAre(
                          std::pair{alice, "2027-02"}, std::pair{bob, "2027-02"},
                          std::pair{alice, "2027-01"}, std::pair{bob, "2027-01"}));

  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  table.alice.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the next game")
                  .has_value());
  now_ms_ = kFebruary + 60'000;
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  EXPECT_EQ(seen().size(), 6u) << "past the first minute, the month alone";
}

// The ask is the finish's: a later commit at the ended table, a seat
// leaving it here, asks nobody again.
TEST_F(ChessFixture, ATableThatHasAlreadyEndedAsksNoOneAgain) {
  std::mutex mu;
  std::vector<std::string> asked;
  golf_->SetChessIndexer([&](const one_d4::IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    asked.push_back(ask.player_id);
  });
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromLeavegame({}))).ok());
  ASSERT_TRUE(
      AwaitChessView(
          table.alice.stream, [](const auto& view) { return view.phase == "closed"; }, "the close")
          .has_value());
  const std::lock_guard<std::mutex> lock(mu);
  EXPECT_EQ(asked.size(), 2u);
}

// A published game's own page reads it by the archive id its [Site]
// names. A game that ended private has an archive id too, and is not
// there: the id is all the route takes, and it reaches no room.
TEST_F(ChessFixture, APublishedGameIsServedByItsArchiveIdAndAPrivateOneIsNot) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());  // private
  ASSERT_TRUE(Ended(table.alice).has_value());
  ASSERT_TRUE(table.alice.stream.Send(Publish(true)).ok());
  ASSERT_TRUE(ReceiveChess(table.alice.stream, "published").has_value());
  ASSERT_TRUE(table.bob.stream.Send(Chess(ChessMove::FromStartgame({}))).ok());
  ASSERT_TRUE(AwaitChessView(
                  table.alice.stream, [](const auto& view) { return view.phase == "playing"; },
                  "the next game")
                  .has_value());
  ASSERT_TRUE(table.alice.stream.Send(Resign()).ok());
  ASSERT_TRUE(Ended(table.alice).has_value());

  ASSERT_TRUE(table.alice.stream.Send(History()).ok());
  auto history = ReceiveChess(table.alice.stream, "history");
  ASSERT_TRUE(history.has_value());
  const auto& games = history->as_history_or_null()->games;
  ASSERT_EQ(games.size(), 2u);
  const int64_t published = games[0].archiveId;
  const int64_t private_game = games[1].archiveId;

  moonbase::games::GetChessGameInput input;
  input.archiveId = published;
  auto game = client_->GetChessGame(input);
  ASSERT_TRUE(game.ok()) << game.error().message();
  const moonbase::games::ChessReview& review = game->review;
  EXPECT_EQ(review.summary.archiveId, published);
  EXPECT_EQ(review.summary.white, games[0].white);
  EXPECT_EQ(review.summary.black, games[0].black);
  EXPECT_TRUE(review.summary.published);
  EXPECT_FALSE(review.summary.gameId.has_value()) << "the page names no table";
  EXPECT_FALSE(review.summary.ordinal.has_value());
  EXPECT_EQ(review.fens.size(), review.moves.size() + 1);
  EXPECT_THAT(review.pgn, ::testing::HasSubstr("/games/chess/" + std::to_string(published) + "\""));
  EXPECT_THAT(review.pgn, ::testing::Not(::testing::HasSubstr(table.room_id)));

  for (const int64_t missing : {private_game, published + 1000}) {
    input.archiveId = missing;
    auto refused = client_->GetChessGame(input);
    ASSERT_FALSE(refused.ok()) << missing;
    EXPECT_EQ(refused.error().code(), "ChessGameNotFound") << missing;
  }
}

}  // namespace
}  // namespace games_hub
