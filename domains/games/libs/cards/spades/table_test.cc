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
Table PlayOut(Table table, const std::vector<int>& bids) {
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
Table Near(const Table& opened, std::vector<int> scores, std::vector<int> bags = {0, 0},
           std::vector<GameScore> games = {}) {
  const auto table = Table::restore(Mode::kPartners, opened.seats(), opened.hand(), 2, scores, bags,
                                    {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, scores, scores}},
                                    std::move(games), std::nullopt);
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

TEST(Table, OpensForFourSeatsWithSeatZeroDealing) {
  EXPECT_EQ(Table::open(Mode::kPartners, {"ann", "bob", "cat"}, FullDeck()).status().code(),
            StatusCode::kInvalidArgument);
  EXPECT_EQ(Table::open(Mode::kPartners, {"ann", "bob", "cat", "ann"}, FullDeck()).status().code(),
            StatusCode::kInvalidArgument);
  const auto table = Table::open(Mode::kPartners, kSeatIds, FullDeck());
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
  const Table table = PlayOut(*Table::open(Mode::kPartners, kSeatIds, FullDeck()), {5, 3, kNil, 2});
  EXPECT_EQ(table.phase(), Phase::kScored);
  EXPECT_EQ(table.hand().tricks(), (std::vector<int>{13, 0, 0, 0}));
  EXPECT_EQ(table.scores(), (std::vector<int>{158, -50}));
  EXPECT_EQ(table.bags(), (std::vector<int>{8, 0}));
  ASSERT_EQ(table.hands().size(), 1u);
  EXPECT_EQ(table.hands()[0], (HandLine{{5, 3, kNil, 2}, {13, 0, 0, 0}, {158, -50}, {158, -50}}));
  EXPECT_EQ(table.winner(), std::nullopt);
  EXPECT_EQ(table.bid(1, 3).status().code(), StatusCode::kFailedPrecondition);
}

TEST(Table, TheDealPassesLeftOnlyOnceTheHandIsScored) {
  const Table opened = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
  EXPECT_EQ(opened.deal(FullDeck()).status().code(), StatusCode::kFailedPrecondition);
  const auto next = PlayOut(opened, {5, 3, kNil, 2}).deal(FullDeck());
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_EQ(next->phase(), Phase::kBidding);
  EXPECT_EQ(next->hand().dealer(), 1);
  EXPECT_EQ(next->hand().turn(), 2);
  EXPECT_EQ(next->handNumber(), 2);
  EXPECT_EQ(next->scores(), (std::vector<int>{158, -50}));
}

// Bags carry from hand to hand.
TEST(Table, BagsCarryIntoTheNextHand) {
  const Table first = PlayOut(*Table::open(Mode::kPartners, kSeatIds, FullDeck()), {5, 3, kNil, 2});
  Table second = *first.deal(FullDeck());
  // Dealt by seat 1, seat 1 holds every spade; seat 3 bids one, so its
  // team is twelve over.
  second = PlayOut(second, {1, 1, 1, 1});
  EXPECT_EQ(second.hand().tricks(), (std::vector<int>{0, 13, 0, 0}));
  EXPECT_EQ(second.scores(), (std::vector<int>{158 - 20, -50 + 20 + 11 - 100}));
  EXPECT_EQ(second.bags(), (std::vector<int>{8, 1}));
}

TEST(Table, AHandThatWinsTheGameEndsItAndTheNextDealStartsAnother) {
  const Table opened = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
  const Table near = Near(opened, {420, 100}, {0, 0}, {{1, {80, 510}}, {1, {200, 505}}});
  const Table won = PlayOut(near, {5, 3, kNil, 2});
  EXPECT_EQ(won.phase(), Phase::kWon);
  EXPECT_EQ(won.winner(), 0);
  EXPECT_EQ(won.gamesWon(), (std::vector<int>{1, 2}));
  const auto fresh = won.deal(FullDeck());
  ASSERT_TRUE(fresh.ok()) << fresh.status();
  EXPECT_EQ(fresh->phase(), Phase::kBidding);
  EXPECT_EQ(fresh->scores(), (std::vector<int>{0, 0}));
  EXPECT_EQ(fresh->bags(), (std::vector<int>{0, 0}));
  EXPECT_TRUE(fresh->hands().empty());
  EXPECT_EQ(fresh->handNumber(), 1);
  EXPECT_EQ(fresh->hand().dealer(), 1);
  EXPECT_EQ(fresh->gamesWon(), (std::vector<int>{1, 2}));
  EXPECT_EQ(fresh->winner(), std::nullopt);
}

TEST(Table, ALeaveClosesTheTable) {
  const auto closed = Table::open(Mode::kPartners, kSeatIds, FullDeck())->removePlayer(2);
  ASSERT_TRUE(closed.ok()) << closed.status();
  EXPECT_TRUE(closed->isOver());
  EXPECT_EQ(closed->phase(), Phase::kClosed);
  EXPECT_EQ(closed->left(), 2);
  EXPECT_EQ(closed->winner(), std::nullopt);
  EXPECT_EQ(closed->bid(1, 3).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(closed->deal(FullDeck()).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(closed->removePlayer(1).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(Table::open(Mode::kPartners, kSeatIds, FullDeck())->removePlayer(4).status().code(),
            StatusCode::kInvalidArgument);
}

TEST(Table, RestoreRefusesWhatPlayCouldNotReach) {
  const Table opened = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
  const Hand hand = opened.hand();
  const std::vector<HandLine> one = {HandLine{{1, 1, 1, 1}, {4, 3, 3, 3}, {10, 10}, {10, 10}}};
  const auto restore = [&](int hand_number, std::vector<int> scores, std::vector<int> bags,
                           std::vector<HandLine> hands, std::vector<GameScore> games,
                           std::optional<int> left) {
    return Table::restore(Mode::kPartners, kSeatIds, hand, hand_number, scores, bags,
                          std::move(hands), std::move(games), left);
  };
  EXPECT_TRUE(restore(1, {0, 0}, {0, 0}, {}, {}, std::nullopt).ok());
  EXPECT_TRUE(restore(2, {10, 10}, {0, 0}, one, {}, std::nullopt).ok());
  EXPECT_FALSE(
      Table::restore(Mode::kPartners, {"ann", "bob"}, hand, 1, {0, 0}, {0, 0}, {}, {}, std::nullopt)
          .ok());
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
  EXPECT_TRUE(Table::restore(Mode::kPartners, kSeatIds, scored.hand(), 1, scored.scores(),
                             scored.bags(), scored.hands(), {}, std::nullopt)
                  .ok());
  std::vector<HandLine> other = scored.hands();
  other.back().bids[0] = 6;
  EXPECT_FALSE(Table::restore(Mode::kPartners, kSeatIds, scored.hand(), 1, scored.scores(),
                              scored.bags(), other, {}, std::nullopt)
                   .ok());
}

// The games a table ended: each won one, and one a leave abandoned.
TEST(Table, KeepsEachGameItEnded) {
  const Table opened = *Table::open(Mode::kPartners, kSeatIds, FullDeck());
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
  EXPECT_EQ(abandoned.gamesWon(), (std::vector<int>{1, 0}));
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

// Cutthroat seats two to four, a team of one a seat; partners seats four.
TEST(Table, CutthroatSeatsTwoToFourEachForItself) {
  for (std::size_t n = 2; n <= 4; ++n) {
    const std::vector<std::string> seats(kSeatIds.begin(), kSeatIds.begin() + n);
    const auto table = Table::open(Mode::kCutthroat, seats, FullDeck());
    ASSERT_TRUE(table.ok()) << n << ": " << table.status();
    EXPECT_EQ(table->teams(), static_cast<int>(n));
    EXPECT_EQ(table->teamOf(static_cast<int>(n) - 1), static_cast<int>(n) - 1);
    EXPECT_EQ(table->scores(), std::vector<int>(n));
    EXPECT_EQ(table->gamesWon(), std::vector<int>(n));
  }
  EXPECT_FALSE(Table::open(Mode::kCutthroat, {"ann"}, FullDeck()).ok());
  EXPECT_FALSE(Table::open(Mode::kCutthroat, {"ann", "bob", "cat", "dan", "eve"}, FullDeck()).ok());
  EXPECT_FALSE(Table::open(Mode::kPartners, {"ann", "bob", "cat"}, FullDeck()).ok());
  EXPECT_EQ(Table::open(Mode::kPartners, kSeatIds, FullDeck())->teams(), 2);
}

// Each seat's hand is scored on its own bid and tricks alone.
TEST(Table, CutthroatScoresEverySeatAlone) {
  const Table opened = *Table::open(Mode::kCutthroat, {"ann", "bob", "cat"}, FullDeck());
  const Table scored = PlayOut(opened, {5, kNil, 6});
  ASSERT_EQ(scored.phase(), Phase::kScored);
  const std::vector<int> tricks = scored.hand().tricks();
  std::vector<int> points;
  for (const TeamScore& team : scoreHand({5, kNil, 6}, tricks, {0, 0, 0})) {
    points.push_back(team.points);
  }
  EXPECT_EQ(scored.scores(), points);
  ASSERT_EQ(scored.hands().size(), 1u);
  EXPECT_EQ(scored.hands().back().points, points);
  EXPECT_EQ(scored.deal(FullDeck())->hand().dealer(), 1);
}

TEST(Table, CutthroatIsWonByOneSeat) {
  const Table opened = *Table::open(Mode::kCutthroat, {"ann", "bob"}, FullDeck());
  // The unshuffled deal goes twelve tricks to ann and one to bob: bid
  // exactly, ann is out at 510 and bob short at 110.
  const auto near =
      Table::restore(Mode::kCutthroat, opened.seats(), opened.hand(), 2, {390, 100}, {0, 0},
                     {HandLine{{1, 1}, {7, 6}, {390, 100}, {390, 100}}}, {}, std::nullopt);
  ASSERT_TRUE(near.ok()) << near.status();
  const Table won = PlayOut(*near, {12, 1});
  EXPECT_EQ(won.scores(), (std::vector<int>{510, 110}));
  ASSERT_EQ(won.phase(), Phase::kWon);
  EXPECT_EQ(won.winner(), 0);
  EXPECT_EQ(won.gamesWon(), (std::vector<int>{1, 0}));
  EXPECT_EQ(won.result()->winner, 0);
  // A row's sizes are the mode's: two teams for two cutthroat seats.
  EXPECT_FALSE(Table::restore(Mode::kCutthroat, opened.seats(), opened.hand(), 1, {0, 0, 0},
                              {0, 0, 0}, {}, {}, std::nullopt)
                   .ok());
}

// A cutthroat row is sized by its seats: a team, a score and bags a seat.
TEST(Table, RestoreRefusesACutthroatRowOfTheWrongShape) {
  const std::vector<std::string> three = {"ann", "bob", "cat"};
  const Table opened = *Table::open(Mode::kCutthroat, three, FullDeck());
  const Hand hand = opened.hand();
  const auto restore = [&](std::vector<std::string> seats, const Hand& held, int number,
                           std::vector<int> scores, std::vector<int> bags,
                           std::vector<HandLine> lines, std::vector<GameScore> games,
                           std::optional<int> left) {
    return Table::restore(Mode::kCutthroat, std::move(seats), held, number, std::move(scores),
                          std::move(bags), std::move(lines), std::move(games), left);
  };
  const std::vector<int> zero = {0, 0, 0};
  EXPECT_TRUE(restore(three, hand, 1, zero, zero, {}, {}, std::nullopt).ok());
  // A hand dealt to another number of seats.
  EXPECT_FALSE(restore({"ann", "bob"}, hand, 1, {0, 0}, {0, 0}, {}, {}, std::nullopt).ok());
  // Bags a team, none below nothing.
  EXPECT_FALSE(restore(three, hand, 1, zero, {0, 0}, {}, {}, std::nullopt).ok());
  EXPECT_FALSE(restore(three, hand, 1, zero, {0, -1, 0}, {}, {}, std::nullopt).ok());
  // The seat that left is one of the three.
  const std::vector<GameScore> abandoned = {{std::nullopt, zero}};
  EXPECT_TRUE(restore(three, hand, 1, zero, zero, {}, abandoned, 2).ok());
  EXPECT_FALSE(restore(three, hand, 1, zero, zero, {}, abandoned, 3).ok());
  // An earlier game: a score a team, won by one of them.
  EXPECT_TRUE(restore(three, hand, 1, zero, zero, {}, {{2, {10, 20, 500}}}, std::nullopt).ok());
  EXPECT_FALSE(restore(three, hand, 1, zero, zero, {}, {{0, {500, 10}}}, std::nullopt).ok());
  EXPECT_FALSE(restore(three, hand, 1, zero, zero, {}, {{3, {10, 20, 500}}}, std::nullopt).ok());
  EXPECT_FALSE(restore(three, hand, 1, zero, zero, {}, {{-1, {10, 20, 500}}}, std::nullopt).ok());
  // A line counts every seat and team.
  const std::vector<int> ten = {10, 10, 10};
  EXPECT_TRUE(restore(three, hand, 2, ten, zero, {HandLine{{1, 1, 1}, {6, 6, 5}, ten, ten}}, {},
                      std::nullopt)
                  .ok());
  EXPECT_FALSE(
      restore(three, hand, 2, ten, zero, {HandLine{{1, 1}, {6, 6, 5}, ten, ten}}, {}, std::nullopt)
          .ok());
  EXPECT_FALSE(restore(three, hand, 2, ten, zero, {HandLine{{1, 1, 1}, {6, 6, 5}, {10, 10}, ten}},
                       {}, std::nullopt)
                   .ok());
  // A hand played out is the last of at least one.
  const Table scored = PlayOut(opened, {5, kNil, 6});
  EXPECT_FALSE(restore(three, scored.hand(), 0, zero, zero, {}, {}, std::nullopt).ok());
}

// The deal passes left round however many seats there are.
TEST(Table, TheDealGoesRoundTheTable) {
  for (std::size_t n = 2; n <= 3; ++n) {
    const std::vector<std::string> seats(kSeatIds.begin(), kSeatIds.begin() + n);
    Table table = *Table::open(Mode::kCutthroat, seats, FullDeck());
    for (std::size_t hand = 1; hand <= n; ++hand) {
      table = *PlayOut(table, std::vector<int>(n, 1)).deal(FullDeck());
      EXPECT_EQ(table.hand().dealer(), static_cast<int>(hand % n)) << n << " seats, hand " << hand;
    }
  }
  const Table three = *Table::open(Mode::kCutthroat, {"ann", "bob", "cat"}, FullDeck());
  EXPECT_EQ(three.removePlayer(3).status().code(), StatusCode::kInvalidArgument);
  EXPECT_TRUE(three.removePlayer(2).ok());
}

}  // namespace
}  // namespace spades
