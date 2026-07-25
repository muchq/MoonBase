#ifndef CPP_CARDS_GOLF_GAME_STATE_SERDE_H
#define CPP_CARDS_GOLF_GAME_STATE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace golf {

/// Serde for the engine's full truth (#1194 step 0): deck order and
/// unrevealed cards included, so serialized states are server-side only —
/// per-viewer redaction stays in the hub's ViewLocked. The wire is
/// versioned JSON; deserialize rejects unknown versions and any value the
/// engine would index out of range, because the bytes arrive from a
/// database row, not from code we trust. Serialization is deterministic:
/// re-serializing a deserialized state reproduces the bytes.
///
/// Schema v1 — cards are Card::intValue() codes (0..51), piles list front
/// to back, hands are [topLeft, topRight, bottomLeft, bottomRight], peeks
/// are indexOfPosition codes (0..3), a null name is an abandoned seat:
///   {"v":1, "gameId":str, "versionId":str,
///    "drawPile":[int...], "discardPile":[int...],
///    "peekedAtDrawPile":bool, "whoseTurn":int, "whoKnocked":int,
///    "peeksHidden":bool,
///    "players":[{"name":str|null, "cards":[int,int,int,int],
///                "peeked":[int...], "donePeeking":bool}...]}
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace golf

#endif
