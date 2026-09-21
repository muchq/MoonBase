#include "domains/games/apis/games_hub/game_events.h"

#include <cstddef>
#include <string>
#include <variant>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace games_hub {

GameFinished FinishedOf(const HostedState& state, std::size_t players) {
  GameFinished finished{GameKindName(KindOf(state)), kCompleted, players};
  if (const auto* golf_state = std::get_if<golf::GameState>(&state)) {
    // The engine supersedes any knock with this sentinel when a leave
    // drops the table below two seats.
    if (golf_state->getWhoKnocked() == golf::GameState::kAbandoned) finished.outcome = kAbandoned;
    return finished;
  }
  if (std::get<castle::GameState>(state).getPhase() == castle::Phase::Abandoned) {
    finished.outcome = kAbandoned;
  }
  return finished;
}

std::string GameFinishedLine(absl::Time when, const GameFinished& finished) {
  // Formatted rather than encoded: every value is a word from a closed
  // vocabulary or a count, so there is nothing here to escape and a JSON
  // dependency would only hide that. GameEventsCarryNoUnescapedText holds
  // it to that.
  return absl::StrFormat(R"({"ts":%d,"event":"%s","variant":"%s","outcome":"%s","players":%d})",
                         absl::ToUnixMillis(when), kGameFinished, finished.variant,
                         finished.outcome, finished.players);
}

}  // namespace games_hub
