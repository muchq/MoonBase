#include "domains/games/apis/games_hub/wire_cards.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "domains/games/libs/cards/card.h"

namespace games_hub {
namespace {

using cards::Card;
using cards::Rank;
using cards::Suit;

moonbase::games::Card Wire(const std::string& rank, const std::string& suit) {
  moonbase::games::Card wire;
  wire.rank = rank;
  wire.suit = suit;
  return wire;
}

TEST(WireCards, EveryCardSpellsOneWayAndReadsBack) {
  for (int i = 0; i < 52; ++i) {
    const Card card{i};
    const moonbase::games::Card wire = WireCard(card);
    const auto back = CardFromWire(wire);
    ASSERT_TRUE(back.has_value()) << wire.rank << wire.suit;
    EXPECT_EQ(*back, card) << wire.rank << wire.suit;
  }
  EXPECT_EQ(WireCard(Card{Suit::Hearts, Rank::Ten}).rank, "10");
  EXPECT_EQ(WireCard(Card{Suit::Hearts, Rank::Ten}).suit, "♥");
}

TEST(WireCards, ASpellingNoCardHasIsNoCard) {
  EXPECT_FALSE(CardFromWire(Wire("A", "H")).has_value());  // the letter, not the glyph
  EXPECT_FALSE(CardFromWire(Wire("1", "♠")).has_value());  // no such rank
  EXPECT_FALSE(CardFromWire(Wire("10", "")).has_value());  // no suit
  EXPECT_FALSE(CardFromWire(Wire("", "♠")).has_value());   // no rank
  EXPECT_FALSE(CardFromWire(Wire("a", "♠")).has_value());  // ranks are upper case
}

TEST(WireCards, NamedCardsResolveToTheirRowSlotsInOrder) {
  const std::vector<Card> row{Card{Suit::Spades, Rank::Queen}, Card{Suit::Clubs, Rank::Seven},
                              Card{Suit::Hearts, Rank::Seven}};
  // Named in any order, addressed in row order.
  EXPECT_EQ(*RowIndexesOf(row, {Wire("7", "♥"), Wire("Q", "♠")}), (std::vector<int>{0, 2}));
  EXPECT_EQ(*RowIndexesOf(row, {Wire("7", "♣")}), (std::vector<int>{1}));
  EXPECT_EQ(*RowIndexesOf(row, {}), (std::vector<int>{}));
}

TEST(WireCards, ACardTheRowDoesNotHoldIsRefusedRatherThanNeighboured) {
  const std::vector<Card> row{Card{Suit::Spades, Rank::Queen}, Card{Suit::Clubs, Rank::Seven}};
  EXPECT_FALSE(RowIndexesOf(row, {Wire("7", "♥")}).ok());
  EXPECT_FALSE(RowIndexesOf(row, {Wire("Q", "♠"), Wire("7", "♥")}).ok());
  EXPECT_FALSE(RowIndexesOf(row, {Wire("Q", "H")}).ok());
  // One slot each: naming a card twice is not a pair.
  EXPECT_FALSE(RowIndexesOf(row, {Wire("Q", "♠"), Wire("Q", "♠")}).ok());
}

TEST(WireCards, ARowHoldingTwoOfACardIsRefusedRatherThanGuessedAt) {
  // A real deal cannot do this; a fixture can, and either slot would be
  // a card the player did not point to.
  const std::vector<Card> row{Card{Suit::Spades, Rank::Five}, Card{Suit::Spades, Rank::Five}};
  EXPECT_FALSE(RowIndexesOf(row, {Wire("5", "♠")}).ok());
}

}  // namespace
}  // namespace games_hub
