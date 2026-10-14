#include "domains/games/apis/games_hub/castle_adapter.h"

namespace games_hub {

std::string Adapter<castle::GameState>::Phase(const castle::GameState& state) {
  switch (state.getPhase()) {
    case castle::Phase::Setup:
      return "setup";
    case castle::Phase::Playing:
      return "playing";
    case castle::Phase::Over:
    case castle::Phase::Abandoned:
      return "ended";
  }
  return "ended";
}

std::string Adapter<castle::GameState>::Turn(const castle::GameState& state) {
  const int seat = state.getWhoseTurn();
  if (seat < 0 || seat >= static_cast<int>(state.getPlayers().size())) return "";
  return state.getPlayer(seat).getId();
}

// The first out wins and everyone seated played. The engine compacts a
// leaver out of its seats, so every seat is on the roster; and a finish
// ends the game, so the winner never left. This leans on an over table
// never being left: StageGameOverLocked erases the entry before anyone
// could. No score: the game has none, so the room's running total stays
// put.
StatsDeltas Adapter<castle::GameState>::ClosedDeltas(const castle::GameState& state,
                                                     const std::vector<std::string>&) {
  std::vector<std::string> seated;
  for (const castle::Player& seat : state.getPlayers()) seated.push_back(seat.getId());
  return PlayedWon(seated, state.getFinished().empty() ? "" : state.getFinished().front());
}

}  // namespace games_hub
