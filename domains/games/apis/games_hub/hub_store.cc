#include "domains/games/apis/games_hub/hub_store.h"

#include <optional>
#include <string>

namespace games_hub {

std::optional<std::string> ChessWinner(const chess_play::GameState& game) {
  if (!game.result().has_value() || !game.result()->winner.has_value()) return std::nullopt;
  return game.players()[game.seatOf(*game.result()->winner)];
}

}  // namespace games_hub
