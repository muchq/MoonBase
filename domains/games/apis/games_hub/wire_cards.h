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
/// CardMapper's letters are a different representation of.
moonbase::games::Card WireCard(const cards::Card& card);

/// The card that spelling names, or nothing if none does. Reads the deck
/// through WireCard rather than a table of its own, so the two
/// directions cannot drift apart.
std::optional<cards::Card> CardFromWire(const moonbase::games::Card& wire);

/// The cards a move names. A spelling no card has, or one card named
/// twice, is the client's own error: it is refused here, before any
/// state is consulted.
absl::StatusOr<std::vector<cards::Card>> CardsFromWire(
    const std::vector<moonbase::games::Card>& named);

/// Where these cards sit in the row, ascending — the translation from
/// what a player names to what the engine addresses. Ascending because
/// the order of the indexes is the order the cards land on the pile, so
/// two clients naming the same cards play the same thing.
///
/// A card the row does not hold is absl::NotFound: a client acting on a
/// view the table has moved past, which the hub could not tell from a
/// good move while every integer in range was a valid address. A real
/// deal holds no duplicates, so a card names one slot; a row that
/// somehow holds two is refused the same way rather than guessed at,
/// since either choice would be a card the player did not point to.
absl::StatusOr<std::vector<int>> RowIndexesOf(const std::vector<cards::Card>& row,
                                              const std::vector<cards::Card>& named);

}  // namespace games_hub

#endif
