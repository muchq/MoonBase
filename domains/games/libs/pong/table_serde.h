#ifndef DOMAINS_GAMES_LIBS_PONG_TABLE_SERDE_H
#define DOMAINS_GAMES_LIBS_PONG_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/pong/table.h"

namespace pong {

/// The games table's column for a pong row: the table, through
/// Table::restore, so a row play could not reach is refused. No rally:
/// a table holds the score, and play resumes from a serve.
///
/// Schema v1:
///   {"v":1, "variant":"smallcat"|"3d", "players":[id, id], "lives":[n, n],
///    "points":n, "inPlay":bool, "closed":bool, "endedByClose":bool,
///    "scoreSheet":[{"winner":playerId, "ending":"points"|"abandoned"}...]}
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace pong

#endif  // DOMAINS_GAMES_LIBS_PONG_TABLE_SERDE_H
