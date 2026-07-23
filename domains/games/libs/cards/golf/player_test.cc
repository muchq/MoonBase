#include "domains/games/libs/cards/golf/player.h"

#include <gtest/gtest.h>

#include <vector>

#include "domains/games/libs/cards/card.h"

using namespace cards;
using namespace golf;

TEST(Player, Score) {
  Player p1{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};  // all twos
  EXPECT_EQ(0, p1.score());

  Player p2{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Three),
            Card(Suit::Hearts, Rank::Four), Card(Suit::Spades, Rank::Five)};
  EXPECT_EQ(14, p2.score());

  Player p3{"Andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Jack), Card(Suit::Spades, Rank::Ace)};
  EXPECT_EQ(1, p3.score());
}

TEST(Player, IsPresent) {
  Player p{Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
           Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  EXPECT_FALSE(p.isPresent());

  Player p1{"ralph", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
            Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  EXPECT_TRUE(p1.isPresent());
}

TEST(Player, ClaimHand) {
  Player p{Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
           Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  EXPECT_FALSE(p.isPresent());

  auto res = p.claimHand("user1");
  EXPECT_TRUE(res.ok());
  EXPECT_TRUE(res->isPresent());
}

TEST(Player, PeeksCapAtTwoAndSurviveSwaps) {
  Player p{"ralph", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
           Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  EXPECT_FALSE(p.hasCompletedPeeks());

  auto one = p.addPeek(Position::TopLeft);
  ASSERT_TRUE(one.ok());
  EXPECT_FALSE(one->addPeek(Position::TopLeft).ok());

  auto two = one->addPeek(Position::BottomRight);
  ASSERT_TRUE(two.ok());
  EXPECT_TRUE(two->hasCompletedPeeks());
  EXPECT_FALSE(two->addPeek(Position::TopRight).ok());

  // The peek record rides through a hand swap.
  Player swapped = two->swapCard(Card(Suit::Hearts, Rank::King), Position::TopLeft);
  EXPECT_EQ(swapped.getPeeked().size(), 2);
  EXPECT_TRUE(swapped.hasCompletedPeeks());

  // clearPeeks hides the cards but the player stays done.
  Player cleared = swapped.clearPeeks();
  EXPECT_TRUE(cleared.getPeeked().empty());
  EXPECT_TRUE(cleared.hasCompletedPeeks());
  EXPECT_FALSE(cleared.addPeek(Position::TopLeft).ok());
}

TEST(Player, PositionIndexMapping) {
  EXPECT_EQ(positionFromIndex(0), Position::TopLeft);
  EXPECT_EQ(positionFromIndex(3), Position::BottomRight);
  EXPECT_FALSE(positionFromIndex(4).has_value());
  EXPECT_FALSE(positionFromIndex(-1).has_value());
  EXPECT_EQ(indexOfPosition(Position::BottomLeft), 2);
}
