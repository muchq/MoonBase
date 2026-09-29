#ifndef CPP_CARDS_RUMMY_TABLE_SERDE_H
#define CPP_CARDS_RUMMY_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/rummy/table.h"

namespace rummy {

/// Serde for a dealer's-choice table (#1609), the games row's state for a
/// rummy table: server-side only, like the deal's.
///
/// Schema v2 — v1 (game_state_serde.h) is a lone deal, which is what a
/// rummy row held before the table:
///   {"v":2, "phase":"choosing"|"playing"|"closed", "seats":[str...],
///    "wins":[int...], "dealer":int, "dealNumber":int, "variant":"basic",
///    "deal":<v1 deal>, "scoreSheet":[{"variant":str, "winner":str?,
///    "points":int}...]}
/// deal is absent before the first deal, scoreSheet before the first deal
/// ends; a hub that predates the sheet ignores it. A v1 row reads as that deal at a
/// table of its seats: in play, dealt by the seat before the one on turn,
/// or closed if the deal had ended, so a finished row stays finished.
///
/// Deserialize rejects what the table could not play: wins not one per
/// seat, a dealer off the table, an open table of fewer than two seats, a
/// playing table without its deal in play of exactly its seats, a table
/// between deals whose deal is still going or whose deal count disagrees
/// with having one. Keys emit alphabetically, so a re-serialize reproduces
/// the bytes; seat ids get the deal's NUL treatment.
[[nodiscard]] std::string serializeTableState(const TableState& table);
[[nodiscard]] absl::StatusOr<TableState> deserializeTableState(const std::string& serialized);

}  // namespace rummy

#endif
