#include "domains/games/libs/cards/spades/table.h"

#include <gtest/gtest.h>

#include <deque>

namespace spades {
namespace {

using absl::StatusCode;

const std::vector<std::string> kSeatIds = {"ann", "bob", "cat", "dan"};

// Unshuffled, card i is suit i % 4, and a deal from dealer d gives seat
// (d + 1 + i) % 4 card i: every seat holds one suit. Dealt by seat 0,
// seat 0 holds every spade and takes every trick.
std::deque<Card> FullDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

// Bids round from the dealer's left, then plays the first legal card of
// whoever is on turn until the hand is out.
Table PlayOut(Table table, const std::array<int, kSeats>& bids) {
  while (table.phase() == Phase::kBidding) {
    const int seat = table.hand().turn();
    table = *table.bid(seat, bids[seat]);
  }
  while (table.phase() == Phase::kPlaying) {
    const int seat = table.hand().turn();
    table = *table.play(seat, table.hand().legalPlays(seat).front());
  }
  return table;
}

TEST(Table, OpensForFourSeatsWithSeatZeroDealing) {
  EXPECT_EQ(Table::open({"ann", "bob", "cat"}, FullDeck()).status().code(),
            StatusCode::kInvalidArgument);
  EXPECT_EQ(Table::open({"ann", "bob", "cat", "ann"}, FullDeck()).status().code(),
            StatusCode::kInvalidArgument);
  const auto table = Table::open(kSeatIds, FullDeck());
  ASSERT_TRUE(table.ok()) << table.status();
  EXPECT_EQ(table->phase(), Phase::kBidding);
  EXPECT_EQ(table->hand().dealer(), 0);
  EXPECT_EQ(table->hand().turn(), 1);
  EXPECT_EQ(table->handNumber(), 1);
  EXPECT_EQ(table->playerIndex("cat"), 2);
  EXPECT_EQ(table->playerIndex("eve"), -1);
}

// Seat 0 takes all thirteen: its nil partner makes the nil, its five is
// eight over, and the other team is set.
TEST(Table, AHandPlayedOutIsScoredAndKept) {
  const Table table = PlayOut(*Table::open(kSeatIds, FullDeck()), {5, 3, kNil, 2});
  EXPECT_EQ(table.phase(), Phase::kScored);
  EXPECT_EQ(table.hand().tricks(), (std::array<int, kSeats>{13, 0, 0, 0}));
  EXPECT_EQ(table.scores(), (std::array<int, kTeams>{158, -50}));
  EXPECT_EQ(table.bags(), (std::array<int, kTeams>{8, 0}));
  ASSERT_EQ(table.scoreSheet().size(), 1u);
  EXPECT_EQ(table.scoreSheet()[0],
            (HandLine{{5, 3, kNil, 2}, {13, 0, 0, 0}, {158, -50}, {158, -50}}));
  EXPECT_EQ(table.winner(), std::nullopt);
  EXPECT_EQ(table.bid(1, 3).status().code(), StatusCode::kFailedPrecondition);
}

TEST(Table, TheDealPassesLeftOnlyOnceTheHandIsScored) {
  const Table opened = *Table::open(kSeatIds, FullDeck());
  EXPECT_EQ(opened.deal(FullDeck()).status().code(), StatusCode::kFailedPrecondition);
  const auto next = PlayOut(opened, {5, 3, kNil, 2}).deal(FullDeck());
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->phase(), Phase::kBidding);
  EXPECT_EQ(next->hand().dealer(), 1);
  EXPECT_EQ(next->hand().turn(), 2);
  EXPECT_EQ(next->handNumber(), 2);
  EXPECT_EQ(next->scores(), (std::array<int, kTeams>{158, -50}));
}

// Bags carry from hand to hand.
TEST(Table, BagsCarryIntoTheNextHand) {
  const Table first = PlayOut(*Table::open(kSeatIds, FullDeck()), {5, 3, kNil, 2});
  Table second = *first.deal(FullDeck());
  // Dealt by seat 1, seat 1 holds every spade; seat 3 bids one, so its
  // team is twelve over.
  second = PlayOut(second, {1, 1, 1, 1});
  EXPECT_EQ(second.hand().tricks(), (std::array<int, kSeats>{0, 13, 0, 0}));
  EXPECT_EQ(second.scores(), (std::array<int, kTeams>{158 - 20, -50 + 20 + 11 - 100}));
  EXPECT_EQ(second.bags(), (std::array<int, kTeams>{8, 1}));
}

TEST(Table, AHandThatWinsTheGameEndsItAndTheNextDealStartsAnother) {
  const Table opened = *Table::open(kSeatIds, FullDeck());
  const Table near =
      *Table::restore(kSeatIds, opened.hand(), 6, {420, 100}, {0, 0}, {}, {0, 2}, std::nullopt);
  const Table won = PlayOut(near, {5, 3, kNil, 2});
  EXPECT_EQ(won.phase(), Phase::kWon);
  EXPECT_EQ(won.winner(), 0);
  EXPECT_EQ(won.gamesWon(), (std::array<int, kTeams>{1, 2}));
  const auto fresh = won.deal(FullDeck());
  ASSERT_TRUE(fresh.ok()) << fresh.status();
  EXPECT_EQ(fresh->phase(), Phase::kBidding);
  EXPECT_EQ(fresh->scores(), (std::array<int, kTeams>{0, 0}));
  EXPECT_EQ(fresh->bags(), (std::array<int, kTeams>{0, 0}));
  EXPECT_TRUE(fresh->scoreSheet().empty());
  EXPECT_EQ(fresh->handNumber(), 1);
  EXPECT_EQ(fresh->hand().dealer(), 1);
  EXPECT_EQ(fresh->gamesWon(), (std::array<int, kTeams>{1, 2}));
  EXPECT_EQ(fresh->winner(), std::nullopt);
}

TEST(Table, ALeaveClosesTheTable) {
  const auto closed = Table::open(kSeatIds, FullDeck())->removePlayer(2);
  ASSERT_TRUE(closed.ok()) << closed.status();
  EXPECT_TRUE(closed->isOver());
  EXPECT_EQ(closed->phase(), Phase::kClosed);
  EXPECT_EQ(closed->left(), 2);
  EXPECT_EQ(closed->winner(), std::nullopt);
  EXPECT_EQ(closed->bid(1, 3).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(closed->deal(FullDeck()).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(closed->removePlayer(1).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(Table::open(kSeatIds, FullDeck())->removePlayer(4).status().code(),
            StatusCode::kInvalidArgument);
}

TEST(Table, RestoreRefusesWhatPlayCouldNotReach) {
  const Hand hand = Table::open(kSeatIds, FullDeck())->hand();
  EXPECT_FALSE(
      Table::restore({"ann", "bob"}, hand, 1, {0, 0}, {0, 0}, {}, {0, 0}, std::nullopt).ok());
  EXPECT_FALSE(Table::restore(kSeatIds, hand, 0, {0, 0}, {0, 0}, {}, {0, 0}, std::nullopt).ok());
  EXPECT_FALSE(Table::restore(kSeatIds, hand, 1, {0, 0}, {10, 0}, {}, {0, 0}, std::nullopt).ok());
  EXPECT_FALSE(Table::restore(kSeatIds, hand, 1, {0, 0}, {0, 0}, {}, {-1, 0}, std::nullopt).ok());
  EXPECT_FALSE(Table::restore(kSeatIds, hand, 1, {0, 0}, {0, 0}, {}, {0, 0}, 5).ok());
  // A game won is a hand played out; in a hand still bid, it is not.
  EXPECT_FALSE(Table::restore(kSeatIds, hand, 1, {510, 0}, {0, 0}, {}, {0, 0}, std::nullopt).ok());
  EXPECT_TRUE(Table::restore(kSeatIds, hand, 1, {0, 0}, {0, 0}, {}, {0, 0}, std::nullopt).ok());
}

TEST(Phase, HasAWireWordEach) {
  EXPECT_EQ(phaseName(Phase::kBidding), "bidding");
  EXPECT_EQ(phaseName(Phase::kPlaying), "playing");
  EXPECT_EQ(phaseName(Phase::kScored), "scored");
  EXPECT_EQ(phaseName(Phase::kWon), "won");
  EXPECT_EQ(phaseName(Phase::kClosed), "closed");
}

}  // namespace
}  // namespace spades
