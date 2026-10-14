#include "domains/games/apis/games_hub/golf_adapter.h"

#include <algorithm>
#include <limits>

namespace games_hub {

std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                     const std::vector<std::string>& roster) {
  std::unordered_set<int> winning;
  int min_score = std::numeric_limits<int>::max();
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    const int score = seat.score();
    if (score < min_score) {
      min_score = score;
      winning.clear();
    }
    if (score == min_score) winning.insert(static_cast<int>(i));
  }
  // The knocker takes ties alone — when still in contention. An
  // abandonment end carries no knocker (kAbandoned is never a seat).
  if (winning.contains(state.getWhoKnocked())) {
    winning.clear();
    winning.insert(state.getWhoKnocked());
  }
  return winning;
}

std::string PhaseString(const golf::GameState& state) {
  if (state.isOver()) return "ended";
  if (state.revealCountdownActive()) return "peeking";
  if (state.getWhoKnocked() >= 0) return "knocked";
  return "playing";
}

std::string PlayerIdAt(const golf::GameState& state, int seat) {
  return state.getPlayer(seat).getName().value_or("");
}

std::string Adapter<golf::GameState>::Turn(const golf::GameState& state) {
  return state.isOver() ? "" : PlayerIdAt(state, state.getWhoseTurn());
}

// Roster seats only: an abandoned seat shows on the scorecard but tallies
// nothing.
StatsDeltas Adapter<golf::GameState>::ClosedDeltas(const golf::GameState& state,
                                                   const std::vector<std::string>& roster) {
  const auto winner_indexes = WinnersAmong(state, roster);
  StatsDeltas deltas;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    HubStore::StatsDelta delta;
    delta.player_id = occupant;
    delta.played = 1;
    delta.won = winner_indexes.contains(static_cast<int>(i)) ? 1 : 0;
    delta.score = seat.score();
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

}  // namespace games_hub
