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
/// Schema v1:
///   {"v":1, "seats":[id x4], "dealer":n, "hands":[[card...] x4],
///    "bids":[n|null x4], "tricks":[n x4],
///    "current":[{"seat":n, "card":c}...],
///    "last":{"plays":[{"seat":n, "card":c} x4], "winner":n} | null,
///    "spadesBroken":bool, "handNumber":n, "scores":[n, n], "bags":[n, n],
///    "lines":[{"bids":[n x4], "tricks":[n x4], "points":[n, n],
///              "scores":[n, n]}...],
///    "games":[{"winner":0|1|null, "scores":[n, n]}...], "left":n | null}
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace spades

#endif  // DOMAINS_GAMES_LIBS_CARDS_SPADES_TABLE_SERDE_H
