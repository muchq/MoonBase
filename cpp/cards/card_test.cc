#include "cpp/cards/card.h"

#include <gtest/gtest.h>

TEST(CARD_TEST, BasicAssertions) {
  cards::Card c_0(0);
  EXPECT_EQ(cards::Suit::Clubs, c_0.getSuit());
  EXPECT_EQ(cards::Rank::Two, c_0.getRank());

  cards::Card c_1(1);
  EXPECT_EQ(cards::Suit::Diamonds, c_1.getSuit());
  EXPECT_EQ(cards::Rank::Two, c_1.getRank());
}
