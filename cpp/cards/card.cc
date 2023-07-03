#include "card.h"

#include <string>

namespace cards {
inline const Card Card::flipped() {
  return Card(suit, rank, static_cast<Facing>((static_cast<int>(facing) + 1) % 2));
}
}  // namespace cards