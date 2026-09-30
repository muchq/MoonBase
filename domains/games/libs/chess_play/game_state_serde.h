#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_SERDE_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {

/// The games table's column for a chess row: versioned JSON of the
/// engine's whole truth. Deserialize goes through GameState::restore, so
/// a row whose moves are illegal, or whose result the moves contradict,
/// is refused rather than played on; the bytes come from a database row,
/// not from code we trust. Unknown versions are refused too.
///
/// Schema v1:
///   {"v":1, "players":[str,str], "variant":"kpk", "whiteSeat":0|1,
///    "startFen":str, "moves":[uci...],
///    "timeControl":{"initialMs":int, "incrementMs":int},
///    "clock":{"whiteMs":int, "blackMs":int, "turnStartedMs":int},
///    "result":{"winner":"white"|"black", "ending":EndingName}}
/// result is absent while the game is on, and its winner absent for a
/// draw.
///
/// Keys emit alphabetically, so re-serializing a deserialized state
/// reproduces the bytes. A player id carrying a NUL byte or invalid
/// UTF-8 (postgres jsonb refuses both) serializes with U+FFFD.
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace chess_play

#endif
