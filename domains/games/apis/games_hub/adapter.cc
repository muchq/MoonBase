#include "domains/games/apis/games_hub/adapter.h"

namespace games_hub {

StatsDeltas PlayedWon(const std::vector<std::string>& players, std::string_view winner) {
  StatsDeltas deltas;
  for (const std::string& id : players) {
    HubStore::StatsDelta delta;
    delta.player_id = id;
    delta.played = 1;
    delta.won = id == winner ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

}  // namespace games_hub
