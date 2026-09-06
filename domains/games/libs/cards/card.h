#ifndef CPP_CARDS_CARD_H
#define CPP_CARDS_CARD_H

namespace cards {

enum class Suit { Clubs, Diamonds, Hearts, Spades };
enum class Rank { Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace };

class Card {
 public:
  Card(const Suit _suit, const Rank _rank) : suit(_suit), rank(_rank) {}

  explicit Card(const int shuffleIndex)
      : suit(static_cast<Suit>(shuffleIndex % 4)), rank(static_cast<Rank>(shuffleIndex / 4)) {}

  [[nodiscard]] const Suit& getSuit() const { return suit; }
  [[nodiscard]] const Rank& getRank() const { return rank; }
  [[nodiscard]] int intValue() const { return static_cast<int>(rank) * 4 + static_cast<int>(suit); }
  bool operator==(const Card& o) const { return suit == o.suit && rank == o.rank; }

 private:
  // Immutable through the interface — no setter, and none belongs here.
  // Not const members: those delete assignment, and a value that cannot
  // be assigned cannot be sorted, erased, or shifted in a container.
  Suit suit;
  Rank rank;
};

};  // namespace cards

#endif
