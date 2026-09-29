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

TEST(Face, RankThenSuit) {
  EXPECT_EQ(faceOf(c(Rank::Ten, Suit::Hearts)), "10♥");
  EXPECT_EQ(faceOf(c(Rank::Ace, Suit::Spades)), "A♠");
  EXPECT_EQ(faceOf(c(Rank::Two)), "2♣");
}

// A card is playable from a pool when some meld of pool cards holds it,
// or it lays off onto a table meld, straight on or after pool cards that
// bridge the way.
TEST(Playable, InASetOrARunOfPoolCards) {
  const vector<Card> set = {c(Rank::Nine), c(Rank::Nine, Suit::Hearts),
                            c(Rank::Nine, Suit::Spades)};
  EXPECT_TRUE(playable(c(Rank::Nine), set, {}));
  const vector<Card> run = {c(Rank::Queen), c(Rank::King), c(Rank::Ace)};
  EXPECT_TRUE(playable(c(Rank::Ace), run, {}));
  EXPECT_TRUE(playable(c(Rank::Queen), run, {}));
  // A pair, a gap, a corner: none of them is a meld.
  EXPECT_FALSE(playable(c(Rank::Nine), {c(Rank::Nine), c(Rank::Nine, Suit::Hearts)}, {}));
  EXPECT_FALSE(playable(c(Rank::Five), {c(Rank::Five), c(Rank::Six), c(Rank::Eight)}, {}));
  EXPECT_FALSE(playable(c(Rank::Two), {c(Rank::King), c(Rank::Ace), c(Rank::Two)}, {}));
}

TEST(Playable, OntoATableMeldStraightOrBridged) {
  const vector<vector<Card>> table = {
      {c(Rank::Five), c(Rank::Six), c(Rank::Seven)},
      {c(Rank::Jack, Suit::Hearts), c(Rank::Jack, Suit::Spades), c(Rank::Jack, Suit::Diamonds)}};
  EXPECT_TRUE(playable(c(Rank::Eight), {c(Rank::Eight)}, table));
  EXPECT_TRUE(playable(c(Rank::Jack), {c(Rank::Jack)}, table));
  // 9♣ needs the 8♣ first; 3♣ needs the 4♣.
  EXPECT_TRUE(playable(c(Rank::Nine), {c(Rank::Nine), c(Rank::Eight)}, table));
  EXPECT_TRUE(playable(c(Rank::Three), {c(Rank::Three), c(Rank::Four)}, table));
  EXPECT_FALSE(playable(c(Rank::Nine), {c(Rank::Nine)}, table));
  EXPECT_FALSE(playable(c(Rank::Three), {c(Rank::Three), c(Rank::Eight)}, table));
  // A set holds four at most.
  const vector<vector<Card>> four = {{c(Rank::Jack, Suit::Hearts), c(Rank::Jack, Suit::Spades),
                                      c(Rank::Jack, Suit::Diamonds), c(Rank::Jack)}};
  EXPECT_FALSE(playable(c(Rank::Jack, Suit::Hearts), {c(Rank::Jack, Suit::Hearts)}, four));
}
