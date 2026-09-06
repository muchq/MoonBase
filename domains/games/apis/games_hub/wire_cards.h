#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WIRE_CARDS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WIRE_CARDS_H

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/card.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// The wire's card language and the way back from it (#1505).
///
/// Ranks are the canonical display strings (A 2..10 J Q K); suits are
/// the v1 wire's glyphs, which the UI already renders and which
/// CardMapper's letters are a different representation of. Both
/// directions live here so a spelling cannot be written one way and read
/// another.
moonbase::games::Card WireCard(const cards::Card& card);

/// The card that spelling names, or nothing if none does.
std::optional<cards::Card> CardFromWire(const moonbase::games::Card& wire);

/// Where these cards sit in the row, ascending — the translation from
/// what a player names to what the engine addresses. Ascending because
/// the order of the indexes is the order the cards land on the pile, so
/// two clients naming the same cards play the same thing.
///
/// Every named card must be in the row exactly once and named once. A
/// real deal holds no duplicates, so a card names one slot; a row that
/// somehow holds two is refused rather than guessed at, since either
/// choice would be a card the player did not point to.
absl::StatusOr<std::vector<int>> RowIndexesOf(const std::vector<cards::Card>& row,
                                              const std::vector<moonbase::games::Card>& named);

}  // namespace games_hub

#endif
