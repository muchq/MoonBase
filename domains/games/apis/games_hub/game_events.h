#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GAME_EVENTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GAME_EVENTS_H

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/hosted_game.h"

namespace games_hub {

/// The hub's domain events (#1571): one JSON object per line, written to
/// disk by //domains/platform/libs/event_log and read months later by the
/// stats pipeline. Not metrics — Prometheus keeps the aggregate and drops
/// the rows, and the rows are the point.
///
/// Four events, which together are the funnel a room goes through:
/// somebody made a room, somebody else walked into it, a table started,
/// the table ended. The access log sees none of it — a session opens one
/// socket and every room, table and game rides that one connection — so
/// this is the only place the shape of an evening is recorded.
///
/// Every value is a closed vocabulary or a count, for the same reason
/// one_d4's query events are (QueryEvent.java): the reader keys rollup
/// rows on them, and an unbounded value is a table that grows without
/// bound. Nothing here identifies a player, a room or a game.

/// The event names, the field every reader filters on first.
inline constexpr std::string_view kRoomCreated = "room_created";
inline constexpr std::string_view kRoomJoined = "room_joined";
inline constexpr std::string_view kGameStarted = "game_started";
inline constexpr std::string_view kGameFinished = "game_finished";

/// The outcomes a game can have. Those are the only two ways one ends.
inline constexpr std::string_view kCompleted = "completed";
inline constexpr std::string_view kAbandoned = "abandoned";

/// A game that ended, as the event describes it.
struct GameFinished {
  /// "golf" or "castle" — GameKindName's spelling, which is also the
  /// wire's and the stored row's.
  std::string_view variant;
  /// kCompleted — the engine played it out — or kAbandoned: too few
  /// seats were left to go on.
  std::string_view outcome;
  /// Seats still held at the end. This is NOT the table's size: an
  /// abandoned game ends at the moment the second-to-last seat leaves,
  /// so it reads 1 for almost every abandonment. The table's size is
  /// game_started's `players`, recorded when the table was still whole.
  std::size_t players;
};

/// How `state` ended, for a roster of `players` seats.
GameFinished FinishedOf(const HostedState& state, std::size_t players);

/// A room was made. Its creator is the only one in it, so there is
/// nothing to count yet.
std::string RoomCreatedLine(absl::Time when);
/// Somebody walked into a room somebody else had made — `players` is the
/// room's size once they were in it, so 2 is the first one that matters.
/// Creating a room is `room_created`, not a join, and a refused join is
/// no event at all.
std::string RoomJoinedLine(absl::Time when, std::size_t players);
/// A table was dealt: which game, and how many seats it was dealt to.
/// The one place the size of a table is recorded while it is still whole.
std::string GameStartedLine(absl::Time when, std::string_view variant, std::size_t players);
/// A table ended. One JSON object, no newline: the log adds it.
std::string GameFinishedLine(absl::Time when, const GameFinished& finished);

}  // namespace games_hub

#endif
