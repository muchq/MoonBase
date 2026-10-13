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

// A table one hand into a game standing at `scores`, about to bid the
// hand of `opened`.
Table Near(const Table& opened, std::array<int, kTeams> scores, std::array<int, kTeams> bags = {},
           std::vector<GameScore> games = {}) {
  const auto table = Table::restore(opened.seats(), opened.hand(), 2, scores, bags,
                                    {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, scores, scores}},
                                    std::move(games), std::nullopt);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
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
  ASSERT_EQ(table.hands().size(), 1u);
  EXPECT_EQ(table.hands()[0], (HandLine{{5, 3, kNil, 2}, {13, 0, 0, 0}, {158, -50}, {158, -50}}));
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
  const Table near = Near(opened, {420, 100}, {}, {{1, {80, 510}}, {1, {200, 505}}});
  const Table won = PlayOut(near, {5, 3, kNil, 2});
  EXPECT_EQ(won.phase(), Phase::kWon);
  EXPECT_EQ(won.winner(), 0);
  EXPECT_EQ(won.gamesWon(), (std::array<int, kTeams>{1, 2}));
  const auto fresh = won.deal(FullDeck());
  ASSERT_TRUE(fresh.ok()) << fresh.status();
  EXPECT_EQ(fresh->phase(), Phase::kBidding);
  EXPECT_EQ(fresh->scores(), (std::array<int, kTeams>{0, 0}));
  EXPECT_EQ(fresh->bags(), (std::array<int, kTeams>{0, 0}));
  EXPECT_TRUE(fresh->hands().empty());
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
  const Table opened = *Table::open(kSeatIds, FullDeck());
  const Hand hand = opened.hand();
  const std::vector<HandLine> one = {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, {10, 10}, {10, 10}}};
  const auto restore = [&](int hand_number, std::array<int, kTeams> scores,
                           std::array<int, kTeams> bags, std::vector<HandLine> hands,
                           std::vector<GameScore> games, std::optional<int> left) {
    return Table::restore(kSeatIds, hand, hand_number, scores, bags, std::move(hands),
                          std::move(games), left);
  };
  EXPECT_TRUE(restore(1, {0, 0}, {0, 0}, {}, {}, std::nullopt).ok());
  EXPECT_TRUE(restore(2, {10, 10}, {0, 0}, one, {}, std::nullopt).ok());
  EXPECT_FALSE(Table::restore({"ann", "bob"}, hand, 1, {0, 0}, {0, 0}, {}, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(1, {0, 0}, {10, 0}, {}, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(1, {0, 0}, {0, 0}, {}, {}, 5).ok());
  // A line for each hand played out, and the scores the last line's.
  EXPECT_FALSE(restore(0, {0, 0}, {0, 0}, {}, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(1, {10, 10}, {0, 0}, one, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(2, {0, 0}, {0, 0}, one, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(1, {300, -50}, {0, 0}, {}, {}, std::nullopt).ok());
  // A game won is a hand played out; in a hand still bid, it is not.
  const std::vector<HandLine> out = {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, {510, 0}, {510, 0}}};
  EXPECT_FALSE(restore(2, {510, 0}, {0, 0}, out, {}, std::nullopt).ok());
  // A leave in a game abandoned it: the games' last.
  EXPECT_FALSE(restore(1, {0, 0}, {0, 0}, {}, {}, 2).ok());
  EXPECT_TRUE(restore(1, {0, 0}, {0, 0}, {}, {{std::nullopt, {0, 0}}}, 2).ok());
  EXPECT_FALSE(restore(1, {0, 0}, {0, 0}, {}, {{3, {0, 0}}}, std::nullopt).ok());
  // A hand played out is the last line.
  const Table scored = PlayOut(opened, {5, 3, kNil, 2});
  EXPECT_TRUE(Table::restore(kSeatIds, scored.hand(), 1, scored.scores(), scored.bags(),
                             scored.hands(), {}, std::nullopt)
                  .ok());
  std::vector<HandLine> other = scored.hands();
  other.back().bids[0] = 6;
  EXPECT_FALSE(Table::restore(kSeatIds, scored.hand(), 1, scored.scores(), scored.bags(), other, {},
                              std::nullopt)
                   .ok());
}

// The games a table ended: each won one, and one a leave abandoned.
TEST(Table, KeepsEachGameItEnded) {
  const Table opened = *Table::open(kSeatIds, FullDeck());
  const Table won = PlayOut(Near(opened, {420, 100}), {5, 3, kNil, 2});
  EXPECT_EQ(won.result(), (GameScore{0, {578, 50}}));
  EXPECT_EQ(won.games(), (std::vector<GameScore>{{0, {578, 50}}}));
  // A leave after the game was won abandons nothing.
  const Table left_after = *won.removePlayer(1);
  EXPECT_FALSE(left_after.abandonedGame());
  EXPECT_EQ(left_after.result(), std::nullopt);
  EXPECT_EQ(left_after.games().size(), 1u);
  // One in the next game abandons it, on the scores it stood at.
  const Table abandoned = *won.deal(FullDeck())->removePlayer(1);
  EXPECT_TRUE(abandoned.abandonedGame());
  EXPECT_EQ(abandoned.result(), (GameScore{std::nullopt, {0, 0}}));
  EXPECT_EQ(abandoned.gamesWon(), (std::array<int, kTeams>{1, 0}));
  // Between hands of a game not yet won, a leave abandons it too.
  const Table between = *PlayOut(opened, {5, 3, kNil, 2}).removePlayer(0);
  EXPECT_EQ(between.result(), (GameScore{std::nullopt, {158, -50}}));
  EXPECT_EQ(opened.result(), std::nullopt);
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
