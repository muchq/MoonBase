#include "cpp/cards/card_mapper.h"

#include <gtest/gtest.h>

#include "cpp/cards/card.h"

using namespace cards;

TEST(CardMapper, CardToString) {
  CardMapper cm;
  Card c(Suit::Clubs, Rank::Two);
  EXPECT_EQ(cm.cardToString(c), "2_C");
}

TEST(CardMapper, CardsToString) {
  CardMapper cm;
  std::vector<Card> cards{0, 1};
  EXPECT_EQ(cm.cardsToString(cards), "[\"2_C\",\"3_D\"]");
}