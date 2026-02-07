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
