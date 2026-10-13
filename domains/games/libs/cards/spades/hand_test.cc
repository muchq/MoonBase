#include "domains/games/libs/cards/spades/hand.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>

namespace spades {
namespace {

using absl::StatusCode;

Card C(Rank rank, Suit suit) { return Card(suit, rank); }

std::deque<Card> FullDeck() {
  std::deque<Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  return deck;
}

// Late in a hand: eleven tricks taken, two cards a seat, everyone bid
// three. Seat 3 took the last trick with the ace of diamonds and leads.
// Spades are not broken.
Hand LateHand(std::vector<Card> seat2 = {C(Rank::Three, Suit::Clubs), C(Rank::Four, Suit::Clubs)},
              bool broken = false,
              std::vector<Card> seat3 = {C(Rank::King, Suit::Hearts),
                                         C(Rank::Nine, Suit::Spades)}) {
  auto hand = Hand::restore(
      /*dealer=*/0,
      {std::vector<Card>{C(Rank::Five, Suit::Hearts), C(Rank::Two, Suit::Spades)},
       std::vector<Card>{C(Rank::Nine, Suit::Hearts), C(Rank::Four, Suit::Diamonds)}, seat2, seat3},
      {3, 3, 3, 3}, {3, 3, 3, 2}, {},
      Trick{{{3, C(Rank::Ace, Suit::Diamonds)},
             {0, C(Rank::Jack, Suit::Diamonds)},
             {1, C(Rank::Two, Suit::Diamonds)},
             {2, C(Rank::Three, Suit::Diamonds)}},
            3},
      broken);
  EXPECT_TRUE(hand.ok()) << hand.status();
  return *hand;
}

TEST(TrickWinner, TheHighestSpadeElseTheHighestOfTheSuitLed) {
  EXPECT_EQ(trickWinner({{0, C(Rank::Ten, Suit::Hearts)},
                         {1, C(Rank::King, Suit::Hearts)},
                         {2, C(Rank::Ace, Suit::Clubs)},
                         {3, C(Rank::Two, Suit::Hearts)}}),
            1);
  EXPECT_EQ(trickWinner({{2, C(Rank::Ace, Suit::Hearts)},
                         {3, C(Rank::Two, Suit::Spades)},
                         {0, C(Rank::Three, Suit::Spades)},
                         {1, C(Rank::King, Suit::Hearts)}}),
            0);
}

TEST(Deal, ThirteenEachRoundFromTheDealersLeftAndBiddingOpensThere) {
  const auto hand = Hand::deal(4, /*dealer=*/1, FullDeck());
  ASSERT_TRUE(hand.ok()) << hand.status();
  for (const auto& held : hand->hands()) EXPECT_EQ(held.size(), 13u);
  EXPECT_EQ(hand->hands()[2].front(), Card(0));
  EXPECT_EQ(hand->hands()[3].front(), Card(1));
  EXPECT_EQ(hand->stage(), Stage::kBidding);
  EXPECT_EQ(hand->turn(), 2);
}

TEST(Deal, RefusesAnythingButOneWholeDeck) {
  std::deque<Card> short_deck = FullDeck();
  short_deck.pop_back();
  EXPECT_EQ(Hand::deal(4, 0, short_deck).status().code(), StatusCode::kInvalidArgument);
  std::deque<Card> doubled = FullDeck();
  doubled.back() = doubled.front();
  EXPECT_EQ(Hand::deal(4, 0, doubled).status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(Hand::deal(4, 4, FullDeck()).status().code(), StatusCode::kInvalidArgument);
}

TEST(Bid, GoesRoundInTurnFromNilToThirteen) {
  auto hand = *Hand::deal(4, 1, FullDeck());
  EXPECT_EQ(hand.bid(3, 2).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_EQ(hand.bid(2, 14).status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(hand.bid(2, -1).status().code(), StatusCode::kInvalidArgument);
  hand = *hand.bid(2, 3);
  hand = *hand.bid(3, kNil);
  hand = *hand.bid(0, 13);
  EXPECT_EQ(hand.stage(), Stage::kBidding);
  hand = *hand.bid(1, 2);
  EXPECT_EQ(hand.bids()[3], 0);
  EXPECT_EQ(hand.stage(), Stage::kPlaying);
  EXPECT_EQ(hand.turn(), 2);
  EXPECT_EQ(hand.bid(2, 3).status().code(), StatusCode::kFailedPrecondition);
}

TEST(Play, NothingIsPlayedWhileBidding) {
  const auto hand = *Hand::deal(4, 1, FullDeck());
  EXPECT_EQ(hand.play(2, hand.hands()[2].front()).status().code(), StatusCode::kFailedPrecondition);
  EXPECT_TRUE(hand.legalPlays(2).empty());
}

TEST(Play, OnlyTheSeatOnTurnPlaysAndOnlyACardItHolds) {
  const Hand hand = LateHand();
  EXPECT_EQ(hand.turn(), 3);
  EXPECT_EQ(hand.play(0, C(Rank::Five, Suit::Hearts)).status().code(),
            StatusCode::kFailedPrecondition);
  EXPECT_EQ(hand.play(3, C(Rank::Two, Suit::Clubs)).status().code(), StatusCode::kNotFound);
  EXPECT_TRUE(hand.legalPlays(0).empty());
}

// Not broken, a spade is led only from a hand of nothing else.
TEST(Play, SpadesAreNotLedUntilBroken) {
  const Hand hand = LateHand();
  EXPECT_EQ(hand.legalPlays(3), (std::vector<Card>{C(Rank::King, Suit::Hearts)}));
  EXPECT_EQ(hand.play(3, C(Rank::Nine, Suit::Spades)).status().code(),
            StatusCode::kInvalidArgument);

  const Hand broken = LateHand({C(Rank::Three, Suit::Clubs), C(Rank::Four, Suit::Clubs)}, true);
  EXPECT_EQ(broken.legalPlays(3).size(), 2u);
  EXPECT_TRUE(broken.play(3, C(Rank::Nine, Suit::Spades)).ok());

  const Hand only_spades =
      LateHand({C(Rank::Three, Suit::Clubs), C(Rank::Four, Suit::Clubs)}, false,
               {C(Rank::Nine, Suit::Spades), C(Rank::Ten, Suit::Spades)});
  EXPECT_EQ(only_spades.legalPlays(3).size(), 2u);
  const auto led = only_spades.play(3, C(Rank::Nine, Suit::Spades));
  ASSERT_TRUE(led.ok()) << led.status();
  EXPECT_TRUE(led->spadesBroken());
}

TEST(Play, ASeatFollowsTheSuitLedIfItCan) {
  const Hand led = *LateHand().play(3, C(Rank::King, Suit::Hearts));
  EXPECT_EQ(led.turn(), 0);
  EXPECT_EQ(led.legalPlays(0), (std::vector<Card>{C(Rank::Five, Suit::Hearts)}));
  EXPECT_EQ(led.play(0, C(Rank::Two, Suit::Spades)).status().code(), StatusCode::kInvalidArgument);
}

// Void in the suit led, a seat plays anything; a spade then takes the
// trick and breaks spades.
TEST(Play, AVoidSeatMayTrump) {
  Hand hand = LateHand({C(Rank::Three, Suit::Spades), C(Rank::Four, Suit::Clubs)});
  hand = *hand.play(3, C(Rank::King, Suit::Hearts));
  hand = *hand.play(0, C(Rank::Five, Suit::Hearts));
  hand = *hand.play(1, C(Rank::Nine, Suit::Hearts));
  EXPECT_EQ(hand.legalPlays(2).size(), 2u);
  hand = *hand.play(2, C(Rank::Three, Suit::Spades));
  EXPECT_TRUE(hand.spadesBroken());
  ASSERT_TRUE(hand.last().has_value());
  EXPECT_EQ(hand.last()->winner, 2);
  EXPECT_EQ(hand.tricks()[2], 4);
  EXPECT_TRUE(hand.current().empty());
  EXPECT_EQ(hand.turn(), 2);
}

TEST(Play, TheWinnerLeadsAndThirteenTricksEndTheHand) {
  Hand hand = LateHand();
  hand = *hand.play(3, C(Rank::King, Suit::Hearts));
  hand = *hand.play(0, C(Rank::Five, Suit::Hearts));
  hand = *hand.play(1, C(Rank::Nine, Suit::Hearts));
  hand = *hand.play(2, C(Rank::Three, Suit::Clubs));
  EXPECT_EQ(hand.last()->winner, 3);
  EXPECT_EQ(hand.turn(), 3);
  hand = *hand.play(3, C(Rank::Nine, Suit::Spades));
  hand = *hand.play(0, C(Rank::Two, Suit::Spades));
  hand = *hand.play(1, C(Rank::Four, Suit::Diamonds));
  hand = *hand.play(2, C(Rank::Four, Suit::Clubs));
  EXPECT_EQ(hand.stage(), Stage::kDone);
  EXPECT_EQ(hand.turn(), kNoTurn);
  EXPECT_EQ(hand.tricks(), (std::vector<int>{3, 3, 3, 4}));
  EXPECT_EQ(hand.play(3, C(Rank::Nine, Suit::Spades)).status().code(),
            StatusCode::kFailedPrecondition);
}

TEST(Restore, RefusesWhatPlayCouldNotReach) {
  const std::vector<std::vector<Card>> two_each = LateHand().hands();
  const Trick last = *LateHand().last();
  // Hands of the wrong size for the tricks taken.
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 3}, {}, last, false).ok());
  // A card twice.
  auto doubled = two_each;
  doubled[1][0] = doubled[0][0];
  EXPECT_FALSE(Hand::restore(0, doubled, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, last, false).ok());
  // A bid out of turn: the dealer's left bids first.
  const auto dealt = Hand::deal(4, 1, FullDeck())->hands();
  EXPECT_FALSE(Hand::restore(1, dealt, {std::nullopt, std::nullopt, std::nullopt, 2}, {0, 0, 0, 0},
                             {}, std::nullopt, false)
                   .ok());
  EXPECT_TRUE(Hand::restore(1, dealt, {std::nullopt, std::nullopt, 2, std::nullopt}, {0, 0, 0, 0},
                            {}, std::nullopt, false)
                  .ok());
  // A spade played with spades unbroken.
  Trick trumped = last;
  trumped.plays[1].card = C(Rank::Queen, Suit::Spades);
  trumped.winner = 0;
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, trumped, false).ok());
  // The trick in play led by someone other than the last winner.
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 2},
                             {{0, C(Rank::Five, Suit::Hearts)}}, last, false)
                   .ok());
  EXPECT_TRUE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, last, false).ok());
}

// What a stored row could hold that play never leaves: a seat outside the
// table, spades broken before any spade was played, a card off the suit
// led by a seat that holds it.
TEST(Restore, RefusesRowsTamperedWith) {
  const std::vector<std::vector<Card>> two_each = LateHand().hands();
  const Trick last = *LateHand().last();
  // Led by "seat -1", seat 0 taking it: in turn, by the arithmetic.
  const Trick off_table{{{-1, C(Rank::Two, Suit::Diamonds)},
                         {0, C(Rank::Ace, Suit::Diamonds)},
                         {1, C(Rank::Jack, Suit::Diamonds)},
                         {2, C(Rank::Three, Suit::Diamonds)}},
                        0};
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, off_table, false).ok());

  const auto dealt = Hand::deal(4, 0, FullDeck())->hands();
  EXPECT_FALSE(Hand::restore(0, dealt, {3, 3, 3, 3}, {0, 0, 0, 0}, {}, std::nullopt, true).ok());
  EXPECT_TRUE(Hand::restore(0, dealt, {3, 3, 3, 3}, {0, 0, 0, 0}, {}, std::nullopt, false).ok());

  // Seat 0 holds the five of hearts, so its spade on a heart lead is no play.
  auto led = two_each;
  led[3] = {C(Rank::Nine, Suit::Spades)};
  led[0] = {C(Rank::Five, Suit::Hearts)};
  const std::vector<TrickPlay> off_suit = {{3, C(Rank::King, Suit::Hearts)},
                                           {0, C(Rank::Two, Suit::Spades)}};
  EXPECT_FALSE(Hand::restore(0, led, {3, 3, 3, 3}, {3, 3, 3, 2}, off_suit, last, true).ok());
  led[0] = {C(Rank::Two, Suit::Spades)};
  const std::vector<TrickPlay> followed = {{3, C(Rank::King, Suit::Hearts)},
                                           {0, C(Rank::Five, Suit::Hearts)}};
  EXPECT_TRUE(Hand::restore(0, led, {3, 3, 3, 3}, {3, 3, 3, 2}, followed, last, false).ok());

  // The same in the trick last taken: seat 1 threw a heart on diamonds
  // while it still holds a diamond.
  Trick discarded = last;
  discarded.plays[2].card = C(Rank::Two, Suit::Hearts);
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, discarded, false).ok());
}

// Bids round from the dealer's left, then the first legal card of whoever
// is on turn until the hand is out.
Hand PlayOut(Hand hand, int bid) {
  while (hand.stage() == Stage::kBidding) hand = *hand.bid(hand.turn(), bid);
  while (hand.stage() == Stage::kPlaying)
    hand = *hand.play(hand.turn(), hand.legalPlays(hand.turn()).front());
  return hand;
}

// Two seats: thirteen each round from the dealer's left; the other
// twenty-six are never seen.
TEST(Deal, TwoSeatsTakeThirteenEachAndTheRestStaysOut) {
  const auto hand = Hand::deal(2, /*dealer=*/1, FullDeck());
  ASSERT_TRUE(hand.ok()) << hand.status();
  ASSERT_EQ(hand->seats(), 2);
  EXPECT_EQ(hand->hands()[0],
            (std::vector<Card>{Card(0), Card(2), Card(4), Card(6), Card(8), Card(10), Card(12),
                               Card(14), Card(16), Card(18), Card(20), Card(22), Card(24)}));
  EXPECT_EQ(hand->hands()[1].size(), 13u);
  EXPECT_EQ(hand->hands()[1].back(), Card(25));
  EXPECT_EQ(hand->turn(), 0);
}

// Three seats: the two of clubs comes out and the other 51 go seventeen
// a seat.
TEST(Deal, ThreeSeatsTakeSeventeenEachWithoutTheTwoOfClubs) {
  const auto hand = Hand::deal(3, /*dealer=*/0, FullDeck());
  ASSERT_TRUE(hand.ok()) << hand.status();
  ASSERT_EQ(hand->seats(), 3);
  for (const auto& held : hand->hands()) {
    EXPECT_EQ(held.size(), 17u);
    EXPECT_EQ(std::count(held.begin(), held.end(), C(Rank::Two, Suit::Clubs)), 0);
  }
  EXPECT_EQ(hand->hands()[1].front(), Card(1));
  EXPECT_EQ(handSize(3), 17);
}

TEST(Deal, SpadesSeatsTwoToFour) {
  EXPECT_EQ(Hand::deal(1, 0, FullDeck()).status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(Hand::deal(5, 0, FullDeck()).status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(Hand::deal(2, 2, FullDeck()).status().code(), StatusCode::kInvalidArgument);
}

// A bid is up to the cards a seat holds: seventeen at three seats.
TEST(Bid, UpToTheCardsInHand) {
  const Hand three = *Hand::deal(3, 0, FullDeck());
  EXPECT_EQ(three.bid(1, 18).status().code(), StatusCode::kInvalidArgument);
  EXPECT_TRUE(three.bid(1, 17).ok());
  const Hand two = *Hand::deal(2, 0, FullDeck());
  EXPECT_EQ(two.bid(1, 14).status().code(), StatusCode::kInvalidArgument);
  EXPECT_TRUE(two.bid(1, 13).ok());
}

// A trick is a card a seat, and a hand as many tricks as cards held.
TEST(Play, ATrickIsACardASeat) {
  Hand two = *Hand::deal(2, 0, FullDeck());
  two = *two.bid(1, 3);
  two = *two.bid(0, 3);
  two = *two.play(1, two.legalPlays(1).front());
  EXPECT_EQ(two.current().size(), 1u);
  two = *two.play(0, two.legalPlays(0).front());
  EXPECT_TRUE(two.current().empty());
  ASSERT_TRUE(two.last().has_value());
  EXPECT_EQ(two.last()->plays.size(), 2u);

  const Hand two_out = PlayOut(*Hand::deal(2, 0, FullDeck()), 3);
  EXPECT_EQ(two_out.tricks()[0] + two_out.tricks()[1], 13);
  const Hand three_out = PlayOut(*Hand::deal(3, 0, FullDeck()), 5);
  EXPECT_EQ(three_out.stage(), Stage::kDone);
  EXPECT_EQ(three_out.tricks()[0] + three_out.tricks()[1] + three_out.tricks()[2], 17);
  EXPECT_EQ(three_out.last()->plays.size(), 3u);
}

// What a stored row could hold that no deal makes: the two of clubs at a
// three-seat table, or a count for a seat that is not there.
TEST(Restore, RefusesWhatNoDealMakes) {
  auto dealt = Hand::deal(3, 0, FullDeck())->hands();
  const std::vector<std::optional<int>> unbid(3);
  EXPECT_TRUE(Hand::restore(0, dealt, unbid, {0, 0, 0}, {}, std::nullopt, false).ok());
  dealt[0][0] = C(Rank::Two, Suit::Clubs);
  EXPECT_FALSE(Hand::restore(0, dealt, unbid, {0, 0, 0}, {}, std::nullopt, false).ok());
  const auto two = Hand::deal(2, 0, FullDeck())->hands();
  EXPECT_FALSE(Hand::restore(0, two, unbid, {0, 0}, {}, std::nullopt, false).ok());
  EXPECT_FALSE(
      Hand::restore(0, two, {std::nullopt, std::nullopt}, {0, 0, 0}, {}, std::nullopt, false).ok());
}

// Two to four seats; a bid up to the cards held; a trick of a card a seat.
TEST(Restore, SizesAreTheSeats) {
  // One seat holding a full thirteen is still no table.
  const auto dealt = Hand::deal(2, 0, FullDeck())->hands();
  EXPECT_FALSE(Hand::restore(0, {dealt[0]}, {std::nullopt}, {0}, {}, std::nullopt, false).ok());
  EXPECT_FALSE(Hand::restore(0, std::vector<std::vector<Card>>(5),
                             std::vector<std::optional<int>>(5), std::vector<int>(5), {},
                             std::nullopt, false)
                   .ok());
  const auto three = Hand::deal(3, 0, FullDeck())->hands();
  EXPECT_TRUE(
      Hand::restore(0, three, {std::nullopt, 17, std::nullopt}, {0, 0, 0}, {}, std::nullopt, false)
          .ok());
  EXPECT_FALSE(
      Hand::restore(0, three, {std::nullopt, 18, std::nullopt}, {0, 0, 0}, {}, std::nullopt, false)
          .ok());
  Hand two = *Hand::deal(2, 0, FullDeck());
  two = *two.bid(1, 3);
  two = *two.bid(0, 3);
  two = *two.play(1, two.legalPlays(1).front());
  two = *two.play(0, two.legalPlays(0).front());
  ASSERT_TRUE(two.last().has_value());
  const auto read = Hand::restore(two.dealer(), two.hands(), two.bids(), two.tricks(), {},
                                  two.last(), two.spadesBroken());
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(*read, two);
}

}  // namespace
}  // namespace spades
