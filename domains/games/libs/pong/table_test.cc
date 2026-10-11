#include "domains/games/libs/pong/table.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include "absl/status/status.h"

namespace pong {
namespace {

using ::testing::ElementsAre;

Table Open(Variant variant = Variant::kSmallcat) {
  auto table = Table::open({"alice", "bob"}, variant);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

TEST(Table, OpensOnAGameInPlayWithEveryLife) {
  const Table table = Open(Variant::kThreeD);
  EXPECT_TRUE(table.inPlay());
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.variant(), Variant::kThreeD);
  EXPECT_THAT(table.players(), ElementsAre("alice", "bob"));
  EXPECT_EQ(table.lives(), (std::array<int, kSeats>{kLives, kLives}));
  EXPECT_EQ(table.points(), 0);
  EXPECT_TRUE(table.scoreSheet().empty());
  EXPECT_EQ(table.playerIndex("bob"), 1);
  EXPECT_EQ(table.playerIndex("carol"), -1);
}

TEST(Table, SeatsTwoDifferentPlayers) {
  EXPECT_FALSE(Table::open({"alice"}, Variant::kSmallcat).ok());
  EXPECT_FALSE(Table::open({"alice", "bob", "carol"}, Variant::kSmallcat).ok());
  EXPECT_FALSE(Table::open({"alice", "alice"}, Variant::kSmallcat).ok());
  EXPECT_FALSE(Table::open({"alice", ""}, Variant::kSmallcat).ok());
}

TEST(Table, AMissCostsTheMisserALife) {
  const auto after = Open().point(1);
  ASSERT_TRUE(after.ok()) << after.status();
  EXPECT_EQ(after->lives(), (std::array<int, kSeats>{kLives, kLives - 1}));
  EXPECT_EQ(after->points(), 1);
  EXPECT_TRUE(after->inPlay());
  EXPECT_TRUE(after->scoreSheet().empty());
}

// The last life ends the game for the other seat; the table stays.
TEST(Table, TheLastLifeEndsTheGameForTheOtherSeat) {
  Table table = Open();
  for (int i = 0; i < kLives; ++i) table = *table.point(0);
  EXPECT_FALSE(table.inPlay());
  EXPECT_FALSE(table.isOver());
  EXPECT_EQ(table.lives()[0], 0);
  ASSERT_EQ(table.scoreSheet().size(), 1u);
  EXPECT_EQ(table.scoreSheet()[0], (GameScore{"bob", Ending::kPoints}));
  EXPECT_EQ(table.point(1).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(Table, APointNamesASeat) {
  EXPECT_EQ(Open().point(2).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(Open().point(-1).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Table, TheNextGameStartsFreshOnlyBetweenGames) {
  EXPECT_EQ(Open().next(Variant::kSmallcat).status().code(), absl::StatusCode::kFailedPrecondition);
  Table table = Open();
  for (int i = 0; i < kLives; ++i) table = *table.point(1);
  const auto next = table.next(Variant::kThreeD);
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_TRUE(next->inPlay());
  EXPECT_EQ(next->variant(), Variant::kThreeD);
  EXPECT_EQ(next->lives(), (std::array<int, kSeats>{kLives, kLives}));
  EXPECT_EQ(next->points(), 0);
  EXPECT_EQ(next->scoreSheet().size(), 1u);
}

TEST(Table, LeavingAGameInPlayAbandonsItToTheOtherSeat) {
  const auto closed = Open().point(1)->removePlayer(1);
  ASSERT_TRUE(closed.ok()) << closed.status();
  EXPECT_TRUE(closed->isOver());
  EXPECT_TRUE(closed->endedByClose());
  EXPECT_FALSE(closed->inPlay());
  ASSERT_EQ(closed->scoreSheet().size(), 1u);
  EXPECT_EQ(closed->scoreSheet()[0], (GameScore{"alice", Ending::kAbandoned}));
  EXPECT_FALSE(closed->removePlayer(0).ok());
  EXPECT_FALSE(closed->next(Variant::kSmallcat).ok());
}

TEST(Table, LeavingBetweenGamesScoresNothing) {
  Table table = Open();
  for (int i = 0; i < kLives; ++i) table = *table.point(1);
  const auto closed = table.removePlayer(0);
  ASSERT_TRUE(closed.ok()) << closed.status();
  EXPECT_TRUE(closed->isOver());
  EXPECT_FALSE(closed->endedByClose());
  EXPECT_EQ(closed->scoreSheet().size(), 1u);
}

TEST(Table, RestoresOnlyWhatPlayCouldReach) {
  const std::vector<std::string> players{"alice", "bob"};
  const std::vector<GameScore> none;
  EXPECT_TRUE(
      Table::restore(players, Variant::kSmallcat, {2, 3}, 1, none, true, false, false).ok());
  // A game in play with a seat out of lives.
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {0, 3}, 3, none, true, false, false).ok());
  // More lives than a game starts with, or fewer than none.
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {4, 3}, 0, none, true, false, false).ok());
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {-1, 3}, 0, none, false, false, false).ok());
  // Closed over a game in play; ended by a close it is not.
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {3, 3}, 0, none, true, true, false).ok());
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {3, 3}, 0, none, false, false, true).ok());
  // A winner who never sat here.
  EXPECT_FALSE(Table::restore(players, Variant::kSmallcat, {3, 0}, 3,
                              {GameScore{"carol", Ending::kPoints}}, false, false, false)
                   .ok());
  // A game over with nothing on the sheet to say so.
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {3, 0}, 3, none, false, false, false).ok());
  EXPECT_FALSE(
      Table::restore({"alice", "alice"}, Variant::kSmallcat, {3, 3}, 0, none, true, false, false)
          .ok());
  EXPECT_FALSE(
      Table::restore(players, Variant::kSmallcat, {3, 3}, -1, none, true, false, false).ok());
}

TEST(Variant, NamesRoundTrip) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    EXPECT_EQ(ParseVariant(VariantName(variant)), variant);
  }
  EXPECT_EQ(VariantName(Variant::kSmallcat), "smallcat");
  EXPECT_EQ(VariantName(Variant::kThreeD), "3d");
  EXPECT_EQ(ParseVariant("chess"), std::nullopt);
  EXPECT_EQ(ParseEnding(EndingName(Ending::kAbandoned)), Ending::kAbandoned);
  EXPECT_EQ(ParseEnding(EndingName(Ending::kPoints)), Ending::kPoints);
  EXPECT_EQ(ParseEnding("checkmate"), std::nullopt);
}

}  // namespace
}  // namespace pong
