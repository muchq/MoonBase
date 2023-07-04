#include "card.h"

#include <unordered_map>

namespace cards {

const Card Card::flipped() {
  return Card(suit, rank, static_cast<Facing>((static_cast<int>(facing) + 1) % 2));
}

const std::string Card::debug_string() {
  std::string repr = "";
  if (RANKS.find(rank) != RANKS.end()) {
    repr.append(RANKS.at(rank));
  } else {
    repr.append("unknown_rank");
  }
  repr.append("_");
  if (SUITS.find(suit) != SUITS.end()) {
    repr.append(SUITS.at(suit));
  } else {
    repr.append("unknown_suit");
  }

  switch (facing) {
    case Facing::Up:
      repr.append("_Up");
    default:
      repr.append("_Down");
  }

  return repr;
}
}  // namespace cards