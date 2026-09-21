#include "domains/games/apis/games_hub/game_events.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/game_state.h"

namespace games_hub {
namespace {

// One line: the timestamp, the name, and whatever fields that event has,
// already rendered with their leading comma.
//
// Formatted rather than encoded: every value is a word from a closed
// vocabulary or a count, so there is nothing here to escape and a JSON
// dependency would only hide that. EveryEventsLineIsTextWithNothingToEscape
// holds it to that.
std::string Line(absl::Time when, std::string_view event, std::string_view fields) {
  return absl::StrFormat(R"({"ts":%d,"event":"%s"%s})", absl::ToUnixMillis(when), event, fields);
}

}  // namespace

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

std::string RoomCreatedLine(absl::Time when) { return Line(when, kRoomCreated, ""); }

std::string RoomJoinedLine(absl::Time when, std::size_t players) {
  return Line(when, kRoomJoined, absl::StrFormat(R"(,"players":%d)", players));
}

std::string GameStartedLine(absl::Time when, std::string_view variant, std::size_t players) {
  return Line(when, kGameStarted,
              absl::StrFormat(R"(,"variant":"%s","players":%d)", variant, players));
}

std::string GameFinishedLine(absl::Time when, const GameFinished& finished) {
  return Line(when, kGameFinished,
              absl::StrFormat(R"(,"variant":"%s","outcome":"%s","players":%d)", finished.variant,
                              finished.outcome, finished.players));
}

}  // namespace games_hub
