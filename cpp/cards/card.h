#ifndef CPP_CARDS_CARD_H
#define CPP_CARDS_CARD_H

#include <string>
#include <unordered_map>

namespace cards {

enum class Suit { Clubs, Diamonds, Hearts, Spades };
enum class Rank { Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace };
enum class Facing { Up, Down };

static const std::unordered_map<Rank, std::string> RANKS{
    {Rank::Two, "2"},   {Rank::Three, "3"}, {Rank::Four, "4"}, {Rank::Five, "5"}, {Rank::Six, "6"},
    {Rank::Seven, "7"}, {Rank::Eight, "8"}, {Rank::Nine, "9"}, {Rank::Ten, "10"}, {Rank::Jack, "J"},
    {Rank::Queen, "Q"}, {Rank::King, "K"},  {Rank::Ace, "A"},
};

static const std::unordered_map<Suit, std::string> SUITS{
    {Suit::Clubs, "C"},
    {Suit::Diamonds, "D"},
    {Suit::Hearts, "H"},
    {Suit::Spades, "S"},
};

class Card {
 public:
  Card(const Suit _suit, const Rank _rank, const Facing _facing)
      : suit(_suit), rank(_rank), facing(_facing) {}
  Card(const Suit _suit, const Rank _rank)
      : suit(_suit), rank(_rank), facing(static_cast<Facing>(1)) {}

  Card(const int shuffleIndex)
      : suit(static_cast<Suit>(shuffleIndex % 4)),
        rank(static_cast<Rank>(shuffleIndex % 13)),
        facing(static_cast<Facing>(1)) {}

  const Card flipped();

  const Suit getSuit() { return suit; }

  const Rank getRank() { return rank; }

  const Facing getFacing() { return facing; }

  bool operator==(const Card& o) const {
    return suit == o.suit && rank == o.rank && facing == o.facing;
  }

  const std::string debug_string();

 private:
  const Suit suit;
  const Rank rank;
  const Facing facing;
};

};  // namespace cards

#endif
