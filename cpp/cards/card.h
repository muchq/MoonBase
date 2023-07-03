#ifndef CPP_CARDS_CARD_H
#define CPP_CARDS_CARD_H

#include <string>

namespace cards {

enum class Suit { Clubs, Diamonds, Hearts, Spades };
enum class Rank { Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace };
enum class Facing { Up, Down };

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

  // fails to link in tests when I define as inline in .cc file... no idea why.
  // error is:
  // bazel-out/k8-fastbuild/bin/cpp/cards/_objs/cards_test/card_test.pic.o:card_test.cc:function
  // CARD_TEST_BasicAssertions_Test::TestBody(): error: undefined reference to
  // 'cards::Card::debug_string[abi:cxx11]()'
  const std::string debug_string() {
    std::string repr = "";
    switch (rank) {
      case Rank::Two:
        repr.append("2");
        break;
      case Rank::Three:
        repr.append("3");
        break;
      case Rank::Four:
        repr.append("4");
        break;
      case Rank::Five:
        repr.append("5");
        break;
      case Rank::Six:
        repr.append("6");
        break;
      case Rank::Seven:
        repr.append("7");
        break;
      case Rank::Eight:
        repr.append("8");
        break;
      case Rank::Nine:
        repr.append("9");
        break;
      case Rank::Ten:
        repr.append("10");
        break;
      case Rank::Jack:
        repr.append("J");
        break;
      case Rank::Queen:
        repr.append("Q");
        break;
      case Rank::King:
        repr.append("K");
        break;
      case Rank::Ace:
        repr.append("A");
        break;
      default:
        repr.append("UNKNOWN");
        break;
    }

    switch (suit) {
      case Suit::Clubs:
        repr.append("_C");
        break;
      case Suit::Diamonds:
        repr.append("_D");
        break;
      case Suit::Hearts:
        repr.append("_H");
        break;
      case Suit::Spades:
        repr.append("_S");
        break;
      default:
        repr.append("_UNKNOWN");
        break;
    }

    switch (facing) {
      case Facing::Up:
        repr.append("_Up");
      default:
        repr.append("_Down");
    }

    return repr;
  }

 private:
  const Suit suit;
  const Rank rank;
  const Facing facing;
};

};  // namespace cards

#endif