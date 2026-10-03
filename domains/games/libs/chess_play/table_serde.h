#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_TABLE_SERDE_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_TABLE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/chess_play/table.h"

namespace chess_play {

/// The games table's column for a chess row: the table, through
/// Table::restore, so a row the game or its sheet contradicts is refused.
///
/// Schema v2:
///   {"v":2, "closed":bool, "endedByClose":bool, "game":<GameState v2>,
///    "scoreSheet":[{"winner":playerId, "winnerColor":"white"|"black",
///                   "ending":EndingName}...]}
/// A line's winner and color are absent for a draw. A bare GameState v1
/// row reads as a table on that game: open while it is in play, closed by
/// its end once over.
[[nodiscard]] std::string serializeTable(const Table& table);
[[nodiscard]] absl::StatusOr<Table> deserializeTable(const std::string& serialized);

}  // namespace chess_play

#endif
