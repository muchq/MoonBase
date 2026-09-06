#include "domains/games/libs/cards/card.h"

#include <gtest/gtest.h>

TEST(CARD_TEST, BasicAssertions) {
  cards::Card c_0(0);
  EXPECT_EQ(cards::Suit::Clubs, c_0.getSuit());
  EXPECT_EQ(cards::Rank::Two, c_0.getRank());

  cards::Card c_1(1);
  EXPECT_EQ(cards::Suit::Diamonds, c_1.getSuit());
  EXPECT_EQ(cards::Rank::Two, c_1.getRank());
}

// Every persisted card is an int and back again (both serdes store the
// value, not the pair), and castle orders a hand by it. Neither holds
// unless the two are inverses across the whole deck.
TEST(CARD_TEST, TheIntValueRoundTripsEveryCardAndOrdersThemByRank) {
  for (int value = 0; value < 52; ++value) {
    const cards::Card card(value);
    EXPECT_EQ(card.intValue(), value);
  }
  for (const cards::Suit suit :
       {cards::Suit::Clubs, cards::Suit::Diamonds, cards::Suit::Hearts, cards::Suit::Spades}) {
    for (int rank = 0; rank <= static_cast<int>(cards::Rank::Ace); ++rank) {
      const cards::Card card(suit, static_cast<cards::Rank>(rank));
      EXPECT_EQ(cards::Card(card.intValue()), card);
    }
  }
  // Rank first, and a tie goes to the deck's own order of suits.
  EXPECT_LT(cards::Card(cards::Suit::Spades, cards::Rank::Two).intValue(),
            cards::Card(cards::Suit::Clubs, cards::Rank::Three).intValue());
  EXPECT_LT(cards::Card(cards::Suit::Clubs, cards::Rank::Ten).intValue(),
            cards::Card(cards::Suit::Clubs, cards::Rank::Jack).intValue());
  EXPECT_LT(cards::Card(cards::Suit::Clubs, cards::Rank::Seven).intValue(),
            cards::Card(cards::Suit::Spades, cards::Rank::Seven).intValue());
}
