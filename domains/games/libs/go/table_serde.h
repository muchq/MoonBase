#ifndef DOMAINS_GAMES_LIBS_GO_TABLE_SERDE_H
#define DOMAINS_GAMES_LIBS_GO_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/go/table.h"

namespace go {

/// The games table's column for a go row: the table, through
/// GameState::restore and Table::restore, so a row play could not reach
/// is refused. The board is never stored; the moves replay to it.
///
/// Schema v1:
///   {"v":1, "players":[id, id], "size":9|13|19, "komi":6.5, "blackSeat":0|1,
///    "moves":[point|-1...], "initialMs":n, "incrementMs":n,
///    "remainingMs":[black, white], "turnStartedMs":n,
///    "result":{"winner":"black"|"white", "ending":...} (absent in play),
///    "closed":bool, "endedByClose":bool,
///    "scoreSheet":[{"winner":playerId, "winnerColor":..., "ending":...}...]}
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace go

#endif  // DOMAINS_GAMES_LIBS_GO_TABLE_SERDE_H
