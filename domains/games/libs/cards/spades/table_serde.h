#ifndef DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_SERDE_H
#define DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/spades/table.h"

namespace spades {

/// The games table's column for a spades row: the full truth, every hand
/// included, read back through Table::restore and Hand::restore so a row
/// play could not reach is refused. Server-side only: redaction is the
/// hub's. A card is its Card::intValue (rank * 4 + suit).
///
/// Schema v1, for S seats and T teams (2 in partners, S in cutthroat):
///   {"v":1, "mode":"partners"|"cutthroat", "seats":[id xS], "dealer":n,
///    "hands":[[card...] xS], "bids":[n|null xS], "tricks":[n xS],
///    "current":[{"seat":n, "card":c}...],
///    "last":{"plays":[{"seat":n, "card":c} xS], "winner":n} | null,
///    "spadesBroken":bool, "handNumber":n, "scores":[n xT], "bags":[n xT],
///    "lines":[{"bids":[n xS], "tricks":[n xS], "points":[n xT],
///              "scores":[n xT]}...],
///    "games":[{"winner":team|null, "scores":[n xT]}...], "left":n | null}
/// A row with no mode is partners: rows written before cutthroat.
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace spades

#endif  // DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_SERDE_H
