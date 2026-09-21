#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GAME_EVENTS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GAME_EVENTS_H

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "domains/games/apis/games_hub/hosted_game.h"

namespace games_hub {

/// The hub's domain events (#1571): one JSON object per game that ended,
/// written to disk by //domains/platform/libs/event_log and read months
/// later by the stats pipeline. Not a metric — Prometheus keeps the
/// aggregate and drops the rows, and the rows are the point.
///
/// Every value is a closed vocabulary or a count, for the same reason
/// one_d4's query events are (QueryEvent.java): the reader keys rollup
/// rows on them, and an unbounded value is a table that grows without
/// bound. Nothing here identifies a player or a room.

/// The event's name, the field every reader filters on first.
inline constexpr std::string_view kGameFinished = "game_finished";

/// A game that ended, as the event describes it.
struct GameFinished {
  /// "golf" or "castle" — GameKindName's spelling, which is also the
  /// wire's and the stored row's.
  std::string_view variant;
  /// "completed" — the engine played it out — or "abandoned": too few
  /// seats were left to go on. Those are the only two ways a game ends.
  std::string_view outcome;
  /// Seats still held when it ended, which for an abandoned game is
  /// fewer than it started with. The hub does not remember a table's
  /// original size once someone leaves it, and inventing one from the
  /// engine would count golf's kept scorecard seats against castle's
  /// compacted ones.
  std::size_t players;
};

inline constexpr std::string_view kCompleted = "completed";
inline constexpr std::string_view kAbandoned = "abandoned";

/// How `state` ended, for a roster of `players` seats.
GameFinished FinishedOf(const HostedState& state, std::size_t players);

/// `finished` as the single line the event log appends. One JSON object,
/// no newline: the log adds it.
std::string GameFinishedLine(absl::Time when, const GameFinished& finished);

}  // namespace games_hub

#endif
