#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ARCHIVE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_ARCHIVE_H

#include <cstdint>
#include <string>

#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// The most finished chess games one answer carries: a room's history,
/// or a page of the public feed. The archive itself is not capped.
inline constexpr int kChessHistoryLimit = 100;

/// An archived game as the history lists it.
moonbase::games::ChessGameSummary ChessSummaryOf(const HubStore::ChessGameRow& row);

/// An archived game as PGN (#1637). It names no room or table: [Site] is
/// the game's own URL, by archive id, unique per game, which is what an
/// indexer keys a game on; [UTCDate] and [UTCTime] say when it ended.
std::string ChessPgnOf(int64_t archive_id, const chess_play::GameState& game, int64_t ended_at_ms);

/// An archived game move by move, for a review.
moonbase::games::ChessReview ChessReviewOf(const HubStore::ChessGameRow& row);

}  // namespace games_hub

#endif
