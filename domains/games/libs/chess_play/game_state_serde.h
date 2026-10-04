#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_SERDE_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {

/// One game as versioned JSON of the engine's whole truth: the `game` of
/// a stored table (table_serde.h), and alone, a v1 row. Deserialize goes
/// through GameState::restore, so a row whose moves are illegal, or whose
/// result the moves contradict, is refused rather than played on; the
/// bytes come from a database row, not from code we trust. Unknown
/// versions are refused too.
///
/// Schema v2:
///   {"v":2, "players":[str,str], "variant":str, "setupId":str,
///    "whiteSeat":0|1, "startFen":str, "moves":[uci...],
///    "timeControl":{"initialMs":int, "incrementMs":int},
///    "clock":{"whiteMs":int, "blackMs":int, "turnStartedMs":int},
///    "result":{"winner":"white"|"black", "ending":EndingName}}
/// result is absent while the game is on, and its winner absent for a
/// draw. A v1 row has no setupId and restores as random-kpk.
///
/// Keys emit alphabetically, so re-serializing a deserialized state
/// reproduces the bytes. A player id carrying a NUL byte or invalid
/// UTF-8 (postgres jsonb refuses both) serializes with U+FFFD.
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace chess_play

#endif
