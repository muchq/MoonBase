#include "cpp/cards/card_mapper.h"

#include <string>
#include <vector>

#include "cpp/cards/card.h"

namespace cards {

static const std::unordered_map<Rank, std::string> RANK_TO_STRING{
    {Rank::Two, "2"},   {Rank::Three, "3"}, {Rank::Four, "4"}, {Rank::Five, "5"}, {Rank::Six, "6"},
    {Rank::Seven, "7"}, {Rank::Eight, "8"}, {Rank::Nine, "9"}, {Rank::Ten, "10"}, {Rank::Jack, "J"},
    {Rank::Queen, "Q"}, {Rank::King, "K"},  {Rank::Ace, "A"},
};

static const std::unordered_map<Suit, std::string> SUIT_TO_STRING{
    {Suit::Clubs, "C"},
    {Suit::Diamonds, "D"},
    {Suit::Hearts, "H"},
    {Suit::Spades, "S"},
};

std::string CardMapper::cardToString(const Card &c) const {
  std::string repr;
  if (RANK_TO_STRING.find(c.getRank()) != RANK_TO_STRING.end()) {
    repr.append(RANK_TO_STRING.at(c.getRank()));
  } else {
    repr.append("unknown_rank");
  }
  repr.append("_");
  if (SUIT_TO_STRING.find(c.getSuit()) != SUIT_TO_STRING.end()) {
    repr.append(SUIT_TO_STRING.at(c.getSuit()));
  } else {
    repr.append("unknown_suit");
  }

  return repr;
}

std::string CardMapper::cardsToString(const std::vector<Card> &cards) const {
  std::string repr;
  repr.append("[");
  for (size_t i = 0; i < cards.size(); i++) {
    repr.append("\"");
    repr.append(cardToString(cards[i]));
    repr.append("\"");
    if (i != cards.size() - 1) {
      repr.append(",");
    }
  }
  repr.append("]");
  return repr;
}

}  // namespace cards
