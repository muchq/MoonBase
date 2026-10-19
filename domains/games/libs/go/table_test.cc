#include "domains/games/libs/go/table.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace go {
namespace {

constexpr int64_t kT0 = 1'800'000'000'000;
constexpr TimeControl kMinute{60'000, 0};

Table Open() {
  auto table = Table::open({"ann", "bo"}, 9, kDefaultKomi, kMinute, kT0);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

Table Resigned(const Table& table, int seat) {
  return *table.inGame([seat](const GameState& game) { return game.resign(seat, kT0); });
}

TEST(Table, OpensWithSeatZeroBlack) {
  const Table table = Open();
  EXPECT_EQ(table.game().blackSeat(), 0);
  EXPECT_TRUE(table.scoreSheet().empty());
  EXPECT_FALSE(table.isOver());
}

// A round robin's pairing names its Black, whichever seat that is; the
// next game still swaps.
TEST(Table, OpensWithTheSeatItNamesBlack) {
  auto table = Table::open({"ann", "bo"}, 9, kDefaultKomi, kMinute, kT0, /*black_seat=*/1);
  ASSERT_TRUE(table.ok()) << table.status();
  EXPECT_EQ(table->game().blackSeat(), 1);
  EXPECT_EQ(table->game().whoseTurn(), 1);
  EXPECT_FALSE(Table::open({"ann", "bo"}, 9, kDefaultKomi, kMinute, kT0, 2).ok());
  auto next = Resigned(*table, 0).next(9, kDefaultKomi, kMinute, kT0);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->game().blackSeat(), 0);
}

TEST(Table, AGamesEndIsALineOnTheSheet) {
  const Table ended = Resigned(Open(), 0);
  ASSERT_EQ(ended.scoreSheet().size(), 1u);
  EXPECT_EQ(ended.scoreSheet()[0], (GameScore{"bo", Ending::kResignation, Color::kWhite}));
  EXPECT_FALSE(ended.isOver());
  EXPECT_EQ(ended.inGame([](const GameState& game) { return game.pass(0, kT0); }).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(Table, TheNextGameSwapsColoursOnTheBoardItNames) {
  EXPECT_EQ(Open().next(9, kDefaultKomi, kMinute, kT0).status().code(),
            absl::StatusCode::kFailedPrecondition);
  const auto next = Resigned(Open(), 0).next(19, 0.5, {120'000, 1'000}, kT0 + 5);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->game().blackSeat(), 1);
  EXPECT_EQ(next->game().whoseTurn(), 1);
  EXPECT_EQ(next->game().size(), 19);
  EXPECT_EQ(next->game().komi(), 0.5);
  EXPECT_EQ(next->scoreSheet().size(), 1u);
  EXPECT_FALSE(next->game().isOver());
}

TEST(Table, ALeaveInAGameAbandonsItAndClosesTheTable) {
  const auto left = Open().removePlayer(1, kT0);
  ASSERT_TRUE(left.ok());
  EXPECT_TRUE(left->isOver());
  EXPECT_TRUE(left->endedByClose());
  EXPECT_EQ(left->scoreSheet().back(), (GameScore{"ann", Ending::kAbandoned, Color::kBlack}));
}

TEST(Table, ALeaveBetweenGamesScoresNothing) {
  const auto left = Resigned(Open(), 1).removePlayer(1, kT0);
  ASSERT_TRUE(left.ok());
  EXPECT_TRUE(left->isOver());
  EXPECT_FALSE(left->endedByClose());
  EXPECT_EQ(left->scoreSheet().size(), 1u);
  EXPECT_FALSE(left->removePlayer(0, kT0).ok());
}

TEST(Table, RestoreRefusesASheetThatDoesNotScoreTheGame) {
  const Table ended = Resigned(Open(), 0);
  EXPECT_FALSE(Table::restore(ended.game(), {}, false, false).ok());
  EXPECT_FALSE(Table::restore(Open().game(), {}, true, false).ok());
  EXPECT_FALSE(Table::restore(Open().game(), {GameScore{"cy", Ending::kTimeout, Color::kBlack}},
                              false, false)
                   .ok());
  EXPECT_TRUE(Table::restore(ended.game(), ended.scoreSheet(), true, false).ok());
}

}  // namespace
}  // namespace go
