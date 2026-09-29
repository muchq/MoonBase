#ifndef CPP_CARDS_RUMMY_ARRANGE_H
#define CPP_CARDS_RUMMY_ARRANGE_H

#include <vector>

#include "domains/games/libs/cards/card.h"

namespace rummy {
using namespace cards;

/// A hand laid out as melds and what is left: the deadwood, and what it
/// costs (cardPoints).
struct Arrangement {
  std::vector<std::vector<Card>> melds;
  std::vector<Card> deadwood;
  int deadwoodPoints = 0;
};

/// The arrangement of `hand` that leaves the least deadwood, each meld
/// laid as arrangedMeld lays it. A card is in one meld at most. Ties go
/// to the first found, which is the same for the same hand.
[[nodiscard]] Arrangement bestArrangement(const std::vector<Card>& hand);

/// A defender's hand against a knock (gin, #1610): its own melds, and
/// the cards it lays off onto the knocker's `onto`, chosen together for
/// the least deadwood. A card laid off is neither meld nor deadwood.
struct LaidOffArrangement {
  Arrangement own;
  std::vector<Card> laidOff;
};

[[nodiscard]] LaidOffArrangement bestWithLayOffs(const std::vector<Card>& hand,
                                                 const std::vector<std::vector<Card>>& onto);

}  // namespace rummy

#endif
