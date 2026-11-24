#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_SERDE_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/wordchain_race/table.h"

namespace wordchain_race {

/// The games table's column for a race row, read back through Race::restore
/// and Table::restore, so a row play could not reach is refused.
///
/// Schema v1:
///   {"v":1, "puzzle":{"start":w, "target":w, "optimal":n, "lexicon":name},
///    "runners":[{"id":playerId, "ladder":[w...], "finishedMs":n (absent
///    until finished), "left":bool}...], "startedMs":n, "deadlineMs":n,
///    "ending":"finished"|"timeout"|"leave" (absent in play),
///    "scoreSheet":[{"winners":[playerId...] (empty for nobody), "ending":...}...],
///    "closed":bool, "endedByClose":bool}
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace wordchain_race

#endif  // DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_SERDE_H
