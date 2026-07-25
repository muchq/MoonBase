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
/// database row, not from code we trust.
///
/// Schema v1 — engine truth only. Identity and optimistic concurrency
/// belong to the storage row (#1194 step 2's game_id and version
/// columns), so gameId/versionId are not in the bytes: deserialize
/// returns them empty, and the storage layer rehydrates via
/// withIdAndVersion. Cards are Card::intValue() codes (0..51), piles
/// list front to back, hands are [topLeft, topRight, bottomLeft,
/// bottomRight], peeks are indexOfPosition codes (0..3), a null name is
/// an abandoned seat:
///   {"v":1, "drawPile":[int...], "discardPile":[int...],
///    "peekedAtDrawPile":bool, "whoseTurn":int, "whoKnocked":int,
///    "peeksHidden":bool,
///    "players":[{"name":str|null, "cards":[int,int,int,int],
///                "peeked":[int...], "donePeeking":bool}...]}
///
/// The listing above is logical; the emitted bytes order keys
/// alphabetically (nlohmann's sorted-map default), which is what makes
/// serialization deterministic — re-serializing a deserialized state
/// reproduces the bytes, and the frozen-payload test pins the exact
/// layout. Two deliberate lenient edges: unknown fields are ignored (and
/// dropped on re-serialize), and game legality (card uniqueness, peek
/// caps) stays the engine's business — serde polices only what it would
/// otherwise let the engine index out of range. A player name that is
/// not valid UTF-8 serializes with U+FFFD replacement rather than
/// failing the write path.
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace golf

#endif
