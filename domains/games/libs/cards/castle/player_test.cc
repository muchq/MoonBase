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
  const Player p{
      "a", {c(Rank::Three), c(Rank::Four), c(Rank::Five)}, {c(Rank::King)}, {c(Rank::Ace)}};
  auto fewer = p.without(Source::Hand, {2, 0});
  ASSERT_TRUE(fewer.ok());
  EXPECT_EQ(fewer->getHand(), (std::vector<Card>{c(Rank::Four)}));
  EXPECT_EQ(fewer->getFaceUp(), p.getFaceUp());

  auto noFaceUp = p.without(Source::FaceUp, {0});
  ASSERT_TRUE(noFaceUp.ok());
  EXPECT_TRUE(noFaceUp->getFaceUp().empty());
  auto noFaceDown = p.without(Source::FaceDown, {0});
  ASSERT_TRUE(noFaceDown.ok());
  EXPECT_TRUE(noFaceDown->getFaceDown().empty());

  EXPECT_FALSE(p.without(Source::Hand, {0, 0}).ok());
  EXPECT_FALSE(p.without(Source::Hand, {3}).ok());
  EXPECT_FALSE(p.without(Source::FaceUp, {1}).ok());
  EXPECT_FALSE(p.without(Source::FaceDown, {-1}).ok());
}

TEST(Player, WithHandAddedAppends) {
  const Player p{"a", {c(Rank::Three)}, {}, {}};
  const Player more = p.withHandAdded({c(Rank::Four), c(Rank::Five)});
  EXPECT_EQ(more.getHand(), (std::vector<Card>{c(Rank::Three), c(Rank::Four), c(Rank::Five)}));
  EXPECT_EQ(p.withHandAdded({}), p);
}
