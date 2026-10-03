#include "domains/games/libs/chess_play/table.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {
namespace {

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";
constexpr int64_t kT0 = 1'000'000;
const TimeControl kClock{180'000, 2'000};

Table Opened(int white_seat = 0) {
  auto table = Table::open({"alice", "bob"}, std::string(GameState::kKpk),
                           Opening{kMate, white_seat}, kClock, kT0);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

// The seat playing White mates.
Table Mated(const Table& table) {
  const int white = table.game().whiteSeat();
  auto next = table.inGame([&](const GameState& game) { return game.move(white, "e7e8q", kT0); });
  EXPECT_TRUE(next.ok()) << next.status();
  return *next;
}

TEST(ChessTable, OpensOnItsFirstGame) {
  const Table table = Opened();
  EXPECT_FALSE(table.isOver());
  EXPECT_FALSE(table.game().isOver());
  EXPECT_TRUE(table.scoreSheet().empty());
  EXPECT_EQ(table.players(), (std::vector<std::string>{"alice", "bob"}));
  EXPECT_EQ(table.playerIndex("bob"), 1);
  EXPECT_EQ(table.playerIndex("carol"), -1);
}

TEST(ChessTable, AGameThatEndsIsScoredAndTheTableStaysOpen) {
  const Table table = Mated(Opened());
  EXPECT_FALSE(table.isOver());
  EXPECT_TRUE(table.game().isOver());
  EXPECT_EQ(table.scoreSheet(),
            (std::vector<GameScore>{{std::string("alice"), Ending::kCheckmate, Color::kWhite}}));
}

TEST(ChessTable, ADrawIsScoredForNobody) {
  auto drawn = Opened().inGame([](const GameState& game) { return game.flag(kT0 + 180'000); });
  ASSERT_TRUE(drawn.ok()) << drawn.status();  // the pawn side's flag against a bare king
  EXPECT_EQ(drawn->scoreSheet(), (std::vector<GameScore>{{std::nullopt, Ending::kTimeout, std::nullopt}}));
}

TEST(ChessTable, AMoveThatDoesNotEndTheGameScoresNothing) {
  auto moved = Opened().inGame([](const GameState& game) { return game.move(0, "g6f6", kT0); });
  ASSERT_TRUE(moved.ok()) << moved.status();
  EXPECT_TRUE(moved->scoreSheet().empty());
}

TEST(ChessTable, AMoveBetweenGamesIsRefused) {
  const auto refused =
      Mated(Opened()).inGame([](const GameState& game) { return game.resign(1, kT0); });
  ASSERT_FALSE(refused.ok());
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ChessTable, TheNextGameSwapsSidesAndKeepsTheSheet) {
  const Table first = Mated(Opened(/*white_seat=*/0));
  // The opening's own seat is overruled: sides alternate.
  auto second =
      first.next("qvr", Opening{kMate, 0}, TimeControl{60'000, 0}, kT0 + 5'000, "qvr-basic");
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(second->game().whiteSeat(), 1);
  EXPECT_EQ(second->game().variant(), "qvr");
  EXPECT_EQ(second->game().setupId(), "qvr-basic");
  EXPECT_FALSE(second->game().isOver());
  EXPECT_EQ(second->game().timeControl(), (TimeControl{60'000, 0}));
  EXPECT_EQ(second->game().players(), first.players());
  EXPECT_EQ(second->scoreSheet(), first.scoreSheet());

  const Table scored = Mated(*second);
  EXPECT_EQ(scored.scoreSheet(),
            (std::vector<GameScore>{{std::string("alice"), Ending::kCheckmate, Color::kWhite},
                                    {std::string("bob"), Ending::kCheckmate, Color::kWhite}}));
}

TEST(ChessTable, TheNextGameWaitsForThisOneToEnd) {
  const auto refused = Opened().next("kpk", Opening{kMate, 0}, kClock, kT0);
  ASSERT_FALSE(refused.ok());
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(ChessTable, LeavingMidGameLosesItAndClosesTheTable) {
  auto left = Opened().removePlayer(0, kT0 + 1'000);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_TRUE(left->isOver());
  EXPECT_TRUE(left->endedByClose());
  EXPECT_EQ(left->scoreSheet(), (std::vector<GameScore>{{std::string("bob"), Ending::kAbandoned, Color::kBlack}}));
}

TEST(ChessTable, LeavingBetweenGamesClosesTheTableAndScoresNothing) {
  auto left = Mated(Opened()).removePlayer(1, kT0 + 1'000);
  ASSERT_TRUE(left.ok()) << left.status();
  EXPECT_TRUE(left->isOver());
  EXPECT_FALSE(left->endedByClose());
  EXPECT_EQ(left->scoreSheet().size(), 1u);
}

TEST(ChessTable, AClosedTablePlaysNoMore) {
  const Table closed = *Mated(Opened()).removePlayer(1, kT0);
  EXPECT_FALSE(closed.next("kpk", Opening{kMate, 0}, kClock, kT0).ok());
  EXPECT_FALSE(closed.removePlayer(0, kT0).ok());
}

TEST(ChessTable, RestoreRefusesASheetTheGameContradicts) {
  const Table mated = Mated(Opened());
  // The control.
  EXPECT_TRUE(Table::restore(mated.game(), mated.scoreSheet(), false, false).ok());
  // An ended game its sheet does not end with.
  EXPECT_FALSE(Table::restore(mated.game(), {}, false, false).ok());
  EXPECT_FALSE(Table::restore(mated.game(), {{std::string("bob"), Ending::kCheckmate, Color::kWhite}}, false, false)
                   .ok());
  // A winner nobody at the table is.
  EXPECT_FALSE(Table::restore(Opened().game(), {{std::string("carol"), Ending::kCheckmate, Color::kWhite}}, false,
                              false)
                   .ok());
  // A winner without the color they won with.
  EXPECT_FALSE(Table::restore(mated.game(), {{std::string("alice"), Ending::kCheckmate, std::nullopt}},
                              false, false)
                   .ok());
  // Closed over a game still in play; ended by a close that did not close it.
  EXPECT_FALSE(Table::restore(Opened().game(), {}, true, false).ok());
  EXPECT_FALSE(Table::restore(mated.game(), mated.scoreSheet(), false, true).ok());
}

}  // namespace
}  // namespace chess_play
