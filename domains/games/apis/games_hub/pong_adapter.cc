#include "domains/games/apis/games_hub/pong_adapter.h"

namespace games_hub {
namespace {

// Both seats played, the winner won. A leaver played and lost.
StatsDeltas GameDeltas(const pong::Table& table) {
  return PlayedWon(table.players(), table.scoreSheet().back().winner.value_or(""));
}

}  // namespace

// A game in play, one ended with the table waiting on the next, or the
// table closed by a leave.
std::string Adapter<pong::Table>::Phase(const pong::Table& table) {
  if (table.isOver()) return "closed";
  return table.inPlay() ? "playing" : "ended";
}

StatsDeltas Adapter<pong::Table>::ClosedDeltas(const pong::Table& table,
                                               const std::vector<std::string>&) {
  return table.endedByClose() ? GameDeltas(table) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<pong::Table>::DealEndDeltas(const pong::Table& before,
                                                               const pong::Table& after) {
  if (!before.inPlay() || after.inPlay() || after.isOver()) return std::nullopt;
  return GameDeltas(after);
}

}  // namespace games_hub
