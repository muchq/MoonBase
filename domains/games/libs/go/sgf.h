#ifndef DOMAINS_GAMES_LIBS_GO_SGF_H
#define DOMAINS_GAMES_LIBS_GO_SGF_H

#include <string>
#include <string_view>

#include "domains/games/libs/go/game_state.h"

namespace go {

/// The game as an SGF (FF[4]) record: board size, komi, players and,
/// once over, its result, then each move, a pass as an empty one. `date`
/// is SGF's DT (YYYY-MM-DD), written when given.
[[nodiscard]] std::string SgfOf(const GameState& game, std::string_view date = {});

}  // namespace go

#endif  // DOMAINS_GAMES_LIBS_GO_SGF_H
