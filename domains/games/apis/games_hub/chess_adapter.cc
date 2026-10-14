#include "domains/games/apis/games_hub/chess_adapter.h"

namespace games_hub {
namespace {

// Both seats played, the winner won, a draw credits nobody. A leaver
// played and lost.
StatsDeltas GameDeltas(const chess_play::GameState& state) {
  const std::optional<chess_play::Result>& result = state.result();
  std::string winner;
  if (result.has_value() && result->winner.has_value()) {
    winner = state.players().at(state.seatOf(*result->winner));
  }
  return PlayedWon(state.players(), winner);
}

}  // namespace

// A game in play, one ended with the table waiting on the next, or the
// table closed by a leave.
std::string Adapter<chess_play::Table>::Phase(const chess_play::Table& table) {
  if (table.isOver()) return "closed";
  return table.game().isOver() ? "ended" : "playing";
}

std::string Adapter<chess_play::Table>::Turn(const chess_play::Table& table) {
  const int seat = table.game().whoseTurn();
  return seat < 0 ? "" : table.players().at(seat);
}

// A table closes by a leave: one that ended a game credits it as any end
// does; one between games, nothing — each game was as it ended.
StatsDeltas Adapter<chess_play::Table>::ClosedDeltas(const chess_play::Table& table,
                                                     const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table.game()) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<chess_play::Table>::DealEndDeltas(
    const chess_play::Table& before, const chess_play::Table& after) {
  if (before.game().isOver() || !after.game().isOver() || after.isOver()) return std::nullopt;
  return GameDeltas(after.game());
}

}  // namespace games_hub
