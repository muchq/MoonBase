#ifndef DOMAINS_GAMES_LIBS_GO_SGF_H
#define DOMAINS_GAMES_LIBS_GO_SGF_H

#include <cstdint>
#include <string>

#include "domains/games/libs/go/game_state.h"

namespace go {

/// The game as an SGF (FF[4]) record: board size, komi, players and,
/// once over, its result, then each move, a pass as an empty one. DT is
/// the UTC day of `ended_at_ms`, epoch milliseconds.
[[nodiscard]] std::string ToSgf(const GameState& game, int64_t ended_at_ms);

}  // namespace go

#endif  // DOMAINS_GAMES_LIBS_GO_SGF_H
