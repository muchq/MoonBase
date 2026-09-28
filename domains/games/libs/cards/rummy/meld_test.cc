#include "domains/games/libs/cards/rummy/meld.h"

#include <gtest/gtest.h>

#include <vector>

#include "domains/games/libs/cards/card.h"

using namespace cards;
using namespace rummy;
using std::vector;

namespace {

Card c(Rank rank, Suit suit = Suit::Clubs) { return Card{suit, rank}; }

}  // namespace

TEST(Meld, ThreeOfARankIsASetLaidInSuitOrder) {
  auto meld = arrangedMeld(
      {c(Rank::Seven, Suit::Spades), c(Rank::Seven, Suit::Clubs), c(Rank::Seven, Suit::Hearts)});
  ASSERT_TRUE(meld.has_value());
  EXPECT_EQ(*meld, (vector<Card>{c(Rank::Seven, Suit::Clubs), c(Rank::Seven, Suit::Hearts),
                                 c(Rank::Seven, Suit::Spades)}));
}

TEST(Meld, FourOfARankIsASet) {
  EXPECT_TRUE(arrangedMeld({c(Rank::King, Suit::Clubs), c(Rank::King, Suit::Diamonds),
                            c(Rank::King, Suit::Hearts), c(Rank::King, Suit::Spades)})
                  .has_value());
}

TEST(Meld, TwoCardsAreNoMeldOfEitherKind) {
  EXPECT_FALSE(arrangedMeld({c(Rank::Seven, Suit::Clubs), c(Rank::Seven, Suit::Hearts)}));
  EXPECT_FALSE(arrangedMeld({c(Rank::Seven), c(Rank::Eight)}));
  EXPECT_FALSE(arrangedMeld({}));
}

TEST(Meld, ARepeatedCardIsNoMeld) {
  // One deck never deals a card twice, so a meld naming one twice is a
  // client error rather than three of a kind.
  EXPECT_FALSE(arrangedMeld({c(Rank::Seven), c(Rank::Seven), c(Rank::Seven, Suit::Hearts)}));
  EXPECT_FALSE(arrangedMeld({c(Rank::Four), c(Rank::Five), c(Rank::Five)}));
}

TEST(Meld, ConsecutiveRanksOfOneSuitAreARunLaidLowToHigh) {
  auto meld = arrangedMeld({c(Rank::Nine), c(Rank::Seven), c(Rank::Eight), c(Rank::Ten)});
  ASSERT_TRUE(meld.has_value());
  EXPECT_EQ(*meld, (vector<Card>{c(Rank::Seven), c(Rank::Eight), c(Rank::Nine), c(Rank::Ten)}));
}

TEST(Meld, ARunWithAGapOrTwoSuitsIsNoMeld) {
  EXPECT_FALSE(arrangedMeld({c(Rank::Seven), c(Rank::Eight), c(Rank::Ten)}));
  EXPECT_FALSE(arrangedMeld({c(Rank::Seven), c(Rank::Eight), c(Rank::Nine, Suit::Hearts)}));
}

TEST(Meld, MixedRanksAndSuitsAreNeitherSetNorRun) {
  EXPECT_FALSE(arrangedMeld(
      {c(Rank::Seven, Suit::Clubs), c(Rank::Seven, Suit::Hearts), c(Rank::Eight, Suit::Hearts)}));
}

TEST(Meld, TheAceRunsLowUnderTheTwo) {
  auto meld = arrangedMeld({c(Rank::Three), c(Rank::Ace), c(Rank::Two)});
  ASSERT_TRUE(meld.has_value());
  EXPECT_EQ(*meld, (vector<Card>{c(Rank::Ace), c(Rank::Two), c(Rank::Three)}));
}

TEST(Meld, TheAceRunsHighOverTheKing) {
  auto meld = arrangedMeld({c(Rank::Ace), c(Rank::King), c(Rank::Queen)});
  ASSERT_TRUE(meld.has_value());
  EXPECT_EQ(*meld, (vector<Card>{c(Rank::Queen), c(Rank::King), c(Rank::Ace)}));
}

TEST(Meld, ARunDoesNotTurnTheCorner) {
  EXPECT_FALSE(arrangedMeld({c(Rank::King), c(Rank::Ace), c(Rank::Two)}));
  EXPECT_FALSE(arrangedMeld({c(Rank::Queen), c(Rank::King), c(Rank::Ace), c(Rank::Two)}));
}

TEST(Meld, AWholeSuitIsOneRun) {
  vector<Card> suit;
  for (int r = 0; r <= static_cast<int>(Rank::Ace); r++) suit.push_back(c(static_cast<Rank>(r)));
  auto meld = arrangedMeld(suit);
  ASSERT_TRUE(meld.has_value());
  // Thirteen ranks and one ace, which fits at either end: the run reads
  // in rank order, the ace on top.
  ASSERT_EQ(meld->size(), 13u);
  EXPECT_EQ(meld->front(), c(Rank::Two));
  EXPECT_EQ(meld->back(), c(Rank::Ace));
}

TEST(CardPoints, AceOnePipsAsPrintedFacesTen) {
  EXPECT_EQ(cardPoints(c(Rank::Ace)), 1);
  EXPECT_EQ(cardPoints(c(Rank::Two)), 2);
  EXPECT_EQ(cardPoints(c(Rank::Ten)), 10);
  EXPECT_EQ(cardPoints(c(Rank::Jack)), 10);
  EXPECT_EQ(cardPoints(c(Rank::Queen)), 10);
  EXPECT_EQ(cardPoints(c(Rank::King)), 10);
}
