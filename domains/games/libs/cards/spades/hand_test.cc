#include "domains/games/libs/cards/spades/hand.h"

#include <gtest/gtest.h>

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
  const auto hand = Hand::deal(/*dealer=*/1, FullDeck());
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
  EXPECT_EQ(Hand::deal(0, short_deck).status().code(), StatusCode::kInvalidArgument);
  std::deque<Card> doubled = FullDeck();
  doubled.back() = doubled.front();
  EXPECT_EQ(Hand::deal(0, doubled).status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(Hand::deal(4, FullDeck()).status().code(), StatusCode::kInvalidArgument);
}

TEST(Bid, GoesRoundInTurnFromNilToThirteen) {
  auto hand = *Hand::deal(1, FullDeck());
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
  const auto hand = *Hand::deal(1, FullDeck());
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
  EXPECT_EQ(hand.tricks(), (std::array<int, kSeats>{3, 3, 3, 4}));
  EXPECT_EQ(hand.play(3, C(Rank::Nine, Suit::Spades)).status().code(),
            StatusCode::kFailedPrecondition);
}

TEST(Restore, RefusesWhatPlayCouldNotReach) {
  const std::array<std::vector<Card>, kSeats> two_each = LateHand().hands();
  const Trick last = *LateHand().last();
  // Hands of the wrong size for the tricks taken.
  EXPECT_FALSE(Hand::restore(0, two_each, {3, 3, 3, 3}, {3, 3, 3, 3}, {}, last, false).ok());
  // A card twice.
  auto doubled = two_each;
  doubled[1][0] = doubled[0][0];
  EXPECT_FALSE(Hand::restore(0, doubled, {3, 3, 3, 3}, {3, 3, 3, 2}, {}, last, false).ok());
  // A bid out of turn: the dealer's left bids first.
  const auto dealt = Hand::deal(1, FullDeck())->hands();
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

}  // namespace
}  // namespace spades
