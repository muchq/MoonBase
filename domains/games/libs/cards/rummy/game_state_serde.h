#ifndef CPP_CARDS_RUMMY_GAME_STATE_SERDE_H
#define CPP_CARDS_RUMMY_GAME_STATE_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/rummy/game_state.h"

namespace rummy {

/// Serde for the engine's full truth, the shape golf's and castle's set:
/// stock order and every hand included, so serialized states are
/// server-side only — per-viewer redaction stays in the hub. The wire is
/// versioned JSON; deserialize rejects unknown versions and any value the
/// engine would index out of range, because the bytes arrive from a
/// database row, not from code we trust.
///
/// Schema v1 — engine truth only; gameId/versionId belong to the storage
/// row and come back empty (the store rehydrates via withIdAndVersion).
/// Cards are Card::intValue() codes (0..51); piles list bottom to top, so
/// the last entry is the top; hands keep their order; melds their table
/// order:
///   {"v":1, "stock":[int...], "discard":[int...], "whoseTurn":int,
///    "stage":"draw"|"play", "phase":"playing"|"over"|"abandoned",
///    "players":[{"id":str, "hand":[int...]}...],
///    "melds":[{"owner":str, "cards":[int...]}...],
///    "takenDiscard":int,
///    "lastMove":{"player":str, "kind":"drawStock"|"drawDiscard"|"meld"|
///                "layOff"|"discard", "cards":[int...], "meld":int}}
/// takenDiscard and lastMove are absent when there is none.
///
/// Keys emit alphabetically (nlohmann's sorted-map default), so
/// re-serializing a deserialized state reproduces the bytes. Unknown
/// fields are ignored. Game legality stays the engine's business: a meld
/// that is no meld reads back as stored. A player id that is not valid
/// UTF-8, or carries a NUL byte (which postgres jsonb refuses), serializes
/// with U+FFFD replacement.
[[nodiscard]] std::string serializeGameState(const GameState& state);
[[nodiscard]] absl::StatusOr<GameState> deserializeGameState(const std::string& serialized);

}  // namespace rummy

#endif
