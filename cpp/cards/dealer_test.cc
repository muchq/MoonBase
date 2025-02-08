#include "cpp/cards/dealer.h"

#include <gtest/gtest.h>

using namespace cards;

TEST(Dealer, DealNewUnshuffledDeck) {
  Dealer dealer;
  auto unshuffled = dealer.DealNewUnshuffledDeck();

  EXPECT_EQ(unshuffled.front().getSuit(), cards::Suit::Clubs);
  EXPECT_EQ(unshuffled.front().getRank(), cards::Rank::Two);

  EXPECT_EQ(unshuffled.back().getSuit(), cards::Suit::Spades);
  EXPECT_EQ(unshuffled.back().getRank(), cards::Rank::Ace);
}

TEST(Dealer, ShuffledDeck) {
  Dealer dealer;
  auto deck = dealer.DealNewUnshuffledDeck();
  dealer.ShuffleDeck(deck);

  auto unshuffled = dealer.DealNewUnshuffledDeck();

  EXPECT_NE(deck, unshuffled);
}
