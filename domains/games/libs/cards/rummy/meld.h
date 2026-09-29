#ifndef CPP_CARDS_RUMMY_MELD_H
#define CPP_CARDS_RUMMY_MELD_H

#include <optional>
#include <vector>

#include "domains/games/libs/cards/card.h"

namespace rummy {
using namespace cards;

/// The cards as they lie on the table if they make a meld, else nothing.
///
/// A set is three or four cards of one rank, laid in suit order. A run is
/// three or more cards of one suit in consecutive ranks, laid low to high;
/// the ace sits below the two or above the king, never both, so a run
/// does not turn the corner (K-A-2 is no run). No card may repeat.
[[nodiscard]] std::optional<std::vector<Card>> arrangedMeld(const std::vector<Card>& cards);

/// What a card left in hand costs at the end: an ace one, two through ten
/// their pips, a face card ten.
[[nodiscard]] int cardPoints(const Card& card);

}  // namespace rummy

#endif
