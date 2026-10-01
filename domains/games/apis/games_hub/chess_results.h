#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_RESULTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_CHESS_RESULTS_H

#include <cstddef>
#include <vector>

#include "domains/games/libs/chess_play/table.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// A finished game's result as the wire says it: the winning player and
/// color, or neither for a draw.
moonbase::games::ChessResult WireChessResult(const chess_play::GameScore& score);

/// The results of the table's games past the first `told`, in order: what
/// gameEnded still owes an instance that has told `told` of them. Read
/// from the score sheet, so a game that ended before the next began is
/// owed all the same.
std::vector<moonbase::games::ChessResult> ChessResultsSince(const chess_play::Table& table,
                                                            std::size_t told);

}  // namespace games_hub

#endif
