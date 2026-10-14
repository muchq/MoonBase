#include "domains/games/apis/games_hub/spades_adapter.h"

namespace games_hub {
namespace {

// Every seat played the game, and the winning team's seats won it. A game
// a leave abandoned was played, and won by nobody.
StatsDeltas GameDeltas(const spades::Table& table, std::optional<int> winning_team) {
  StatsDeltas deltas;
  for (int seat = 0; seat < table.hand().seats(); ++seat) {
    HubStore::StatsDelta delta;
    delta.player_id = table.seats()[seat];
    delta.played = 1;
    delta.won = winning_team == table.teamOf(seat) ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

}  // namespace

std::string Adapter<spades::Table>::Turn(const spades::Table& table) {
  // A closed table has no turn; between hands the hand names none.
  const int seat = table.isOver() ? spades::kNoTurn : table.hand().turn();
  return seat < 0 ? "" : table.seats().at(seat);
}

// A table closes by a leave: a game it abandoned was played; one already
// won was credited as it was won.
StatsDeltas Adapter<spades::Table>::ClosedDeltas(const spades::Table& table,
                                                 const std::vector<std::string>&) {
  return table.abandonedGame() ? GameDeltas(table, std::nullopt) : StatsDeltas{};
}

std::optional<StatsDeltas> Adapter<spades::Table>::DealEndDeltas(const spades::Table& before,
                                                                 const spades::Table& after) {
  if (before.phase() == spades::Phase::kWon || after.phase() != spades::Phase::kWon) {
    return std::nullopt;
  }
  return GameDeltas(after, after.winner());
}

}  // namespace games_hub
