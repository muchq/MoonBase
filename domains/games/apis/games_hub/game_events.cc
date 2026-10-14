#include "domains/games/apis/games_hub/game_events.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/adapters.h"

namespace games_hub {
namespace {

// One line: the timestamp, the event's name, the room it happened in,
// and whatever fields that event has, already rendered with their
// leading comma.
//
// Formatted rather than encoded: every value is a word from a closed
// vocabulary, a count, or a room id RoomTag has already reduced to one
// — so there is nothing here to escape, and a JSON dependency would only
// hide that. EveryEventsLineIsTextWithNothingToEscape holds it to that.
std::string Line(absl::Time when, std::string_view event, std::string_view room,
                 std::string_view fields) {
  return absl::StrFormat(R"({"ts":%d,"event":"%s","room":"%s"%s})", absl::ToUnixMillis(when), event,
                         RoomTag(room), fields);
}

}  // namespace

std::string RoomTag(std::string_view room) {
  std::string tag(room);
  for (char& c : tag) {
    if (!absl::ascii_isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
  }
  return tag;
}

std::optional<GameFinished> FinishedOf(const HostedState& state, std::size_t players) {
  const std::optional<GameEnd> ended = VisitAdapter(
      state, [](auto adapter, const auto& engine) { return decltype(adapter)::Ended(engine); });
  if (!ended.has_value()) return std::nullopt;
  return GameFinished{VariantWordOf(state),
                      *ended == GameEnd::kAbandoned ? kOutcomeAbandoned : kOutcomeCompleted,
                      players};
}

std::string RoomCreatedLine(absl::Time when, std::string_view room, std::string_view surface) {
  return Line(when, kEventRoomCreated, room, absl::StrFormat(R"(,"surface":"%s")", surface));
}

std::string GeometryChangedLine(absl::Time when, std::string_view room, std::string_view surface) {
  return Line(when, kEventGeometryChanged, room, absl::StrFormat(R"(,"surface":"%s")", surface));
}

std::string RoomJoinedLine(absl::Time when, std::string_view room, std::size_t players) {
  return Line(when, kEventRoomJoined, room, absl::StrFormat(R"(,"players":%d)", players));
}

std::string RoomClosedLine(absl::Time when, std::string_view room) {
  return Line(when, kEventRoomClosed, room, "");
}

std::string ChatMessageLine(absl::Time when, std::string_view room, std::size_t players) {
  return Line(when, kEventChatMessage, room, absl::StrFormat(R"(,"players":%d)", players));
}

std::string GameStartedLine(absl::Time when, std::string_view room, std::string_view variant,
                            std::size_t players) {
  return Line(when, kEventGameStarted, room,
              absl::StrFormat(R"(,"variant":"%s","players":%d)", variant, players));
}

std::string GameFinishedLine(absl::Time when, std::string_view room, const GameFinished& finished) {
  return Line(when, kEventGameFinished, room,
              absl::StrFormat(R"(,"variant":"%s","outcome":"%s","players":%d)", finished.variant,
                              finished.outcome, finished.players));
}

}  // namespace games_hub
