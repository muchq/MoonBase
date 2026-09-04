#ifndef CPP_CARDS_CASTLE_GAME_STATE_SERDE_H
#define CPP_CARDS_CASTLE_GAME_STATE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/castle/game_state.h"

namespace castle {

/// Serde for the engine's full truth, the shape golf's serde set (#1194
/// step 0): draw pile order and every hand included, so serialized states
/// are server-side only — per-viewer redaction stays in the hub. The wire
/// is versioned JSON; deserialize rejects unknown versions and any value
/// the engine would index out of range, because the bytes arrive from a
/// database row, not from code we trust.
///
/// Schema v1 — engine truth only; gameId/versionId belong to the storage
/// row and come back empty (the store rehydrates via withIdAndVersion).
/// Cards are Card::intValue() codes (0..51); piles list front to back, so
/// the pile's last entry is its top; rows keep their table order:
///   {"v":1, "drawPile":[int...], "pile":[int...], "whoseTurn":int,
///    "phase":"setup"|"playing"|"over"|"abandoned", "finished":[str...],
///    "players":[{"id":str, "hand":[int...], "faceUp":[int...],
///                "faceDown":[int...], "ready":bool}...]}
///
/// Keys emit alphabetically (nlohmann's sorted-map default), so
/// re-serializing a deserialized state reproduces the bytes. Unknown
/// fields are ignored; game legality stays the engine's business. A
/// player id that is not valid UTF-8, or carries a NUL byte (which
/// postgres jsonb refuses), serializes with U+FFFD replacement.
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace castle

#endif
