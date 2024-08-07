#include "cpp/cards/card.h"

#include <gtest/gtest.h>

TEST(CARD_TEST, BasicAssertions) {
  cards::Card c(0);
  EXPECT_EQ(cards::Suit::Clubs, c.getSuit());
  EXPECT_EQ(cards::Rank::Two, c.getRank());
}
