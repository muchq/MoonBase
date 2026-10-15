// Go on the room stream, end to end through the generated client. The
// hub's clock is fixed here: time moves only when a test moves it.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::GoMove;
using moonbase::games::GoView;

constexpr int64_t kT0 = 1'800'000'000'000;

GameCommands Play(int x, int y) {
  moonbase::games::GoPlay play;
  play.x = x;
  play.y = y;
  return Go(GoMove::FromPlay(play));
}

GameCommands Pass() { return Go(GoMove::FromPass(moonbase::games::GoPass{})); }

GameCommands Resign() { return Go(GoMove::FromResign(moonbase::games::GoResign{})); }

GameCommands Leave() { return Go(GoMove::FromLeavegame(moonbase::games::LeaveGame{})); }

GameCommands StartGo(moonbase::games::GoStartGame start) {
  return Go(GoMove::FromStartgame(std::move(start)));
}

moonbase::games::GoStartGame NineByNine() {
  moonbase::games::GoStartGame start;
  start.size = 9;
  start.initialSeconds = 60;
  start.incrementSeconds = 0;
  return start;
}

class GoFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    GamesHubStreamFixture::SetUp();
    hub_->SetClock([this] { return absl::FromUnixMillis(now_ms_.load()); });
  }

  // A two-seat go table started on `start`: alice (seat 0, Black) created
  // it, bob joined. Each seat has heard gameStarted and read its first
  // view.
  struct Started {
    Table table;
    GoView view;
  };
  std::optional<Started> StartedTable(moonbase::games::GoStartGame start = NineByNine()) {
    auto room = SeatedRoom(2);
    if (!room.has_value()) return std::nullopt;
    Table table{std::move(room->seats[0]), std::move(room->seats[1]), room->room_id, ""};
    if (!table.alice.stream.Send(Go(GoMove::FromCreategame(moonbase::games::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    auto created = ReceiveGo(table.alice.stream, "gameJoined");
    if (!created.has_value()) return std::nullopt;
    table.game_id = created->as_gameJoined_or_null()->view.gameId;
    moonbase::games::JoinGame join;
    join.gameId = table.game_id;
    if (!table.bob.stream.Send(Go(GoMove::FromJoingame(join))).ok()) return std::nullopt;
    if (!ReceiveGo(table.bob.stream, "gameJoined").has_value()) return std::nullopt;
    if (!table.alice.stream.Send(StartGo(std::move(start))).ok()) return std::nullopt;
    std::optional<GoView> view;
    for (Seat* seat : {&table.alice, &table.bob}) {
      if (!ReceiveGo(seat->stream, "gameStarted").has_value()) return std::nullopt;
      auto state = ReceiveGo(seat->stream, "gameState");
      if (!state.has_value()) return std::nullopt;
      view = state->as_gameState_or_null()->view;
    }
    return Started{std::move(table), *view};
  }

  // The next view `seat` hears that `ready` holds of, skipping older ones.
  static std::optional<GoView> ViewWhere(Seat& seat,
                                         const std::function<bool(const GoView&)>& ready) {
    for (int i = 0; i < 16; ++i) {
      auto state = ReceiveGo(seat.stream, "gameState");
      if (!state.has_value()) return std::nullopt;
      const GoView& view = state->as_gameState_or_null()->view;
      if (ready(view)) return view;
    }
    ADD_FAILURE() << "no such view for " << seat.player_id;
    return std::nullopt;
  }

  static std::optional<moonbase::games::GoResult> Ended(Seat& seat) {
    auto ended = ReceiveGo(seat.stream, "gameEnded");
    if (!ended.has_value()) return std::nullopt;
    return ended->as_gameEnded_or_null()->result;
  }

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

  std::atomic<int64_t> now_ms_{kT0};
};

TEST_F(GoFixture, AThirdSeatIsRefused) {
  ExpectSeatCap<GoMove>(2, Go,
                        [](moonbase::games::PlayClientStream& stream, const std::string& wanted) {
                          return ReceiveGo(stream, wanted);
                        });
}

TEST_F(GoFixture, AStartedTableShowsAnEmptyBoardBlackToMoveAndTheClock) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  const GoView& view = started->view;
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.size, 9);
  EXPECT_EQ(view.komi, 6.5);
  ASSERT_EQ(view.board.size(), 9u);
  EXPECT_EQ(view.board[0], ".........");
  ASSERT_EQ(view.players.size(), 2u);
  EXPECT_EQ(view.players[0].playerId, started->table.alice.player_id);
  EXPECT_EQ(view.players[0].color, "black");
  EXPECT_EQ(view.players[1].color, "white");
  EXPECT_EQ(view.sideToMove, "black");
  EXPECT_EQ(view.currentPlayerId, started->table.alice.player_id);
  EXPECT_EQ(view.moveCount, 0);
  EXPECT_FALSE(view.lastMove.has_value());
  ASSERT_TRUE(view.clock.has_value());
  EXPECT_EQ(view.clock->blackMs, 60'000);
  EXPECT_EQ(view.clock->whiteMs, 60'000);
  EXPECT_FALSE(view.result.has_value());
}

TEST_F(GoFixture, AMoveOffTurnIsRefused) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  ASSERT_TRUE(started->table.bob.stream.Send(Play(4, 4)).ok());
  auto refused = ReceiveCase(started->table.bob.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "not your turn");
}

TEST_F(GoFixture, ACaptureTakesTheStoneOffTheBoard) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play(1, 0)).ok());
  ASSERT_TRUE(ViewWhere(table.bob, [](const GoView& v) { return v.moveCount == 1; }));
  ASSERT_TRUE(table.bob.stream.Send(Play(0, 0)).ok());
  ASSERT_TRUE(ViewWhere(table.alice, [](const GoView& v) { return v.moveCount == 2; }));
  ASSERT_TRUE(table.alice.stream.Send(Play(0, 1)).ok());
  auto view = ViewWhere(table.bob, [](const GoView& v) { return v.moveCount == 3; });
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->board[0], ".b.......");
  EXPECT_EQ(view->board[1], "b........");
  ASSERT_TRUE(view->captures.has_value());
  EXPECT_EQ(view->captures->black, 1);
  EXPECT_EQ(view->captures->white, 0);
  ASSERT_TRUE(view->lastMove.has_value());
  EXPECT_EQ(view->lastMove->color, "black");
  EXPECT_FALSE(view->lastMove->pass);
  EXPECT_EQ(view->lastMove->x, 0);
  EXPECT_EQ(view->lastMove->y, 1);
  EXPECT_EQ(view->currentPlayerId, table.bob.player_id);
}

// Black's one stone owns the whole board: 81 to White's komi.
TEST_F(GoFixture, TwoPassesEndTheGameOnTheScoreAndTheNextSwapsColours) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Play(4, 4)).ok());
  ASSERT_TRUE(ReceiveGo(table.bob.stream, "turnChanged").has_value());
  ASSERT_TRUE(table.bob.stream.Send(Pass()).ok());
  ASSERT_TRUE(ViewWhere(table.alice, [](const GoView& v) { return v.moveCount == 2; }));
  ASSERT_TRUE(table.alice.stream.Send(Pass()).ok());
  auto ended_view = ViewWhere(table.bob, [](const GoView& v) { return v.phase == "ended"; });
  ASSERT_TRUE(ended_view.has_value());
  EXPECT_FALSE(ended_view->currentPlayerId.has_value());
  ASSERT_EQ(ended_view->scoreSheet.size(), 1u);
  EXPECT_EQ(ended_view->scoreSheet[0].ending, "score");
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto result = Ended(*seat);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winner, table.alice.player_id);
    EXPECT_EQ(result->winnerColor, "black");
    EXPECT_EQ(result->ending, "score");
    ASSERT_TRUE(result->score.has_value());
    EXPECT_EQ(result->score->black, 81);
    EXPECT_EQ(result->score->white, 6.5);
  }
  const auto records = RecordsOnce(table.bob, [&](const auto& room) {
    return std::any_of(room.players.begin(), room.players.end(),
                       [&](const auto& p) { return p.gamesWon == 1; });
  });
  EXPECT_EQ(records.at(table.alice.player_id), std::make_pair(1, 1));
  EXPECT_EQ(records.at(table.bob.player_id), std::make_pair(1, 0));

  moonbase::games::GoStartGame next;
  next.size = 13;
  ASSERT_TRUE(table.bob.stream.Send(StartGo(next)).ok());
  auto again = ViewWhere(table.alice, [](const GoView& v) { return v.phase == "playing"; });
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->size, 13);
  EXPECT_EQ(again->players[0].playerId, table.bob.player_id);
  EXPECT_EQ(again->players[0].color, "black");
  EXPECT_EQ(again->currentPlayerId, table.bob.player_id);
  EXPECT_EQ(again->scoreSheet.size(), 1u);
}

TEST_F(GoFixture, EitherSeatMayResignOffTurn) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.bob.stream.Send(Resign()).ok());
  auto result = Ended(table.alice);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.alice.player_id);
  EXPECT_EQ(result->ending, "resignation");
  EXPECT_FALSE(result->score.has_value());
}

TEST_F(GoFixture, TheSweepEndsAGameWhoseSideToMoveRanOutOfTime) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  now_ms_ += 59'999;
  // Black's minute, less a millisecond: nothing to sweep.
  EXPECT_EQ(hub_->SweepChessClocksOnce(), 0);

  now_ms_ += 1;
  EXPECT_EQ(hub_->SweepChessClocksOnce(), 1);
  for (Seat* seat : {&table.alice, &table.bob}) {
    auto result = Ended(*seat);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->winner, table.bob.player_id);
    EXPECT_EQ(result->winnerColor, "white");
    EXPECT_EQ(result->ending, "timeout");
  }
  EXPECT_EQ(hub_->SweepChessClocksOnce(), 0);
}

TEST_F(GoFixture, ALeaverLosesByAbandonment) {
  auto started = StartedTable();
  ASSERT_TRUE(started.has_value());
  Table& table = started->table;
  ASSERT_TRUE(table.alice.stream.Send(Leave()).ok());
  auto closed = ViewWhere(table.bob, [](const GoView& v) { return v.phase == "closed"; });
  ASSERT_TRUE(closed.has_value());
  ASSERT_EQ(closed->scoreSheet.size(), 1u);
  EXPECT_EQ(closed->scoreSheet[0].winner, table.bob.player_id);
  EXPECT_EQ(closed->scoreSheet[0].ending, "abandoned");
  auto result = Ended(table.bob);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->winner, table.bob.player_id);
  EXPECT_EQ(result->ending, "abandoned");
}

}  // namespace
}  // namespace games_hub
