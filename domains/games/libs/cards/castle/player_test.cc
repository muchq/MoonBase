#include "domains/games/libs/cards/castle/player.h"

#include <gtest/gtest.h>

#include <vector>

#include "domains/games/libs/cards/card.h"

using namespace cards;
using namespace castle;

namespace {

Card c(Rank rank, Suit suit = Suit::Clubs) { return Card{suit, rank}; }

}  // namespace

TEST(Player, TheActiveRowIsHandThenFaceUpThenFaceDown) {
  const Player full{"a", {c(Rank::Three)}, {c(Rank::Four)}, {c(Rank::Five)}};
  EXPECT_EQ(full.source(), Source::Hand);
  const Player noHand{"a", {}, {c(Rank::Four)}, {c(Rank::Five)}};
  EXPECT_EQ(noHand.source(), Source::FaceUp);
  const Player castleOnly{"a", {}, {}, {c(Rank::Five)}};
  EXPECT_EQ(castleOnly.source(), Source::FaceDown);
}

TEST(Player, RowNamesEachOfTheThreeRows) {
  const Player p{"a", {c(Rank::Three)}, {c(Rank::Four)}, {c(Rank::Five)}};
  EXPECT_EQ(p.row(Source::Hand), (std::vector<Card>{c(Rank::Three)}));
  EXPECT_EQ(p.row(Source::FaceUp), (std::vector<Card>{c(Rank::Four)}));
  EXPECT_EQ(p.row(Source::FaceDown), (std::vector<Card>{c(Rank::Five)}));
}

TEST(Player, ASeatIsOutWhenEveryRowIsEmpty) {
  const Player full{"a", {c(Rank::Three)}, {c(Rank::Four)}, {c(Rank::Five)}};
  EXPECT_EQ(full.cardsLeft(), 3);
  EXPECT_FALSE(full.isOut());
  const Player out{"a", {}, {}, {}};
  EXPECT_TRUE(out.isOut());
  EXPECT_EQ(out.cardsLeft(), 0);
}

TEST(Player, SetupSwapExchangesAHandCardWithAFaceUpCard) {
  const Player p{"a", {c(Rank::Three), c(Rank::Four)}, {c(Rank::King), c(Rank::Ace)}, {}};
  auto swapped = p.swapForSetup(1, 0);
  ASSERT_TRUE(swapped.ok());
  EXPECT_EQ(swapped->getHand(), (std::vector<Card>{c(Rank::Three), c(Rank::King)}));
  EXPECT_EQ(swapped->getFaceUp(), (std::vector<Card>{c(Rank::Four), c(Rank::Ace)}));
  EXPECT_FALSE(swapped->isReady());

  EXPECT_FALSE(p.swapForSetup(2, 0).ok());
  EXPECT_FALSE(p.swapForSetup(-1, 0).ok());
  EXPECT_FALSE(p.swapForSetup(0, 2).ok());
  EXPECT_TRUE(p.withReady().isReady());
}

TEST(Player, WithoutRemovesTheNamedCardsAndKeepsTheOrder) {
  const Player p{"a",
                 {c(Rank::Three), c(Rank::Four), c(Rank::Five)},
                 {c(Rank::King), c(Rank::Two), c(Rank::Queen)},
                 {c(Rank::Ace), c(Rank::Six)}};
  auto fewer = p.without(Source::Hand, {2, 0});
  ASSERT_TRUE(fewer.ok());
  EXPECT_EQ(fewer->getHand(), (std::vector<Card>{c(Rank::Four)}));
  EXPECT_EQ(fewer->getFaceUp(), p.getFaceUp());

  // What is left of a table row holds its place: the row is dealt, not
  // sorted, and its index is what pairs it with the row beneath.
  auto played = p.without(Source::FaceUp, {1});
  ASSERT_TRUE(played.ok());
  EXPECT_EQ(played->getFaceUp(), (std::vector<Card>{c(Rank::King), c(Rank::Queen)}));
  auto flipped = p.without(Source::FaceDown, {0});
  ASSERT_TRUE(flipped.ok());
  EXPECT_EQ(flipped->getFaceDown(), (std::vector<Card>{c(Rank::Six)}));

  auto noFaceUp = p.without(Source::FaceUp, {0, 1, 2});
  ASSERT_TRUE(noFaceUp.ok());
  EXPECT_TRUE(noFaceUp->getFaceUp().empty());
  auto noFaceDown = p.without(Source::FaceDown, {0, 1});
  ASSERT_TRUE(noFaceDown.ok());
  EXPECT_TRUE(noFaceDown->getFaceDown().empty());

  EXPECT_FALSE(p.without(Source::Hand, {0, 0}).ok());
  EXPECT_FALSE(p.without(Source::Hand, {3}).ok());
  EXPECT_FALSE(p.without(Source::FaceUp, {3}).ok());
  EXPECT_FALSE(p.without(Source::FaceDown, {-1}).ok());
}

TEST(Player, WithHandAddedSortsThePileIntoTheHand) {
  const Player p{"a", {c(Rank::Three), c(Rank::Ten)}, {}, {}};
  // A pile taken up is not a tail on the hand: each card lands at its rank.
  const Player more = p.withHandAdded({c(Rank::Ace), c(Rank::Four), c(Rank::Two)});
  EXPECT_EQ(more.getHand(), (std::vector<Card>{c(Rank::Two), c(Rank::Three), c(Rank::Four),
                                               c(Rank::Ten), c(Rank::Ace)}));
  EXPECT_EQ(p.withHandAdded({}), p);
}

TEST(Player, TheHandReadsInRankOrderHoweverItsCardsArrive) {
  const Player p{
      "a",
      {c(Rank::King), c(Rank::Two), c(Rank::Seven, Suit::Spades), c(Rank::Seven, Suit::Diamonds)},
      {},
      {}};
  // Rank first; a tie goes to the deck's own order of suits.
  EXPECT_EQ(p.getHand(), (std::vector<Card>{c(Rank::Two), c(Rank::Seven, Suit::Diamonds),
                                            c(Rank::Seven, Suit::Spades), c(Rank::King)}));
  // What is left after a play is still in order.
  auto fewer = p.without(Source::Hand, {0});
  ASSERT_TRUE(fewer.ok());
  EXPECT_EQ(fewer->getHand(), (std::vector<Card>{c(Rank::Seven, Suit::Diamonds),
                                                 c(Rank::Seven, Suit::Spades), c(Rank::King)}));
}

TEST(Player, TheTableRowsKeepTheOrderTheyWereDealtIn) {
  // Face-up pairs with face-down by index, and that pairing is the
  // castle: sorting either row would deal it again.
  const Player p{"a", {}, {c(Rank::King), c(Rank::Two)}, {c(Rank::Ace), c(Rank::Three)}};
  EXPECT_EQ(p.getFaceUp(), (std::vector<Card>{c(Rank::King), c(Rank::Two)}));
  EXPECT_EQ(p.getFaceDown(), (std::vector<Card>{c(Rank::Ace), c(Rank::Three)}));
}
