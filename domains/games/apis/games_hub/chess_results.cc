#include "domains/games/apis/games_hub/chess_results.h"

#include <string>

namespace games_hub {

moonbase::games::ChessResult WireChessResult(const chess_play::GameScore& score) {
  moonbase::games::ChessResult wire;
  wire.ending = std::string(chess_play::EndingName(score.ending));
  wire.winner = score.winner;
  if (score.winner_color.has_value()) {
    wire.winnerColor = std::string(chess_play::ColorName(*score.winner_color));
  }
  return wire;
}

std::vector<moonbase::games::ChessResult> ChessResultsSince(const chess_play::Table& table,
                                                            std::size_t told) {
  std::vector<moonbase::games::ChessResult> owed;
  const auto& sheet = table.scoreSheet();
  for (std::size_t i = told; i < sheet.size(); ++i) owed.push_back(WireChessResult(sheet[i]));
  return owed;
}

}  // namespace games_hub
