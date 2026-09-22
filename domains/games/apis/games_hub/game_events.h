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
/// Seven events, which together are the shape of an evening: somebody
/// made a room, somebody else walked into it, they reshaped the world
/// they were standing in, they talked, a table started, the table ended,
/// and the last of them left. The access log sees none of it — a session
/// opens one socket and every room, world, table, game and message rides
/// that one connection — so this is the only place it is recorded.
///
/// Every line carries the room it happened in, which is what makes this
/// a session rather than seven counters: one room's evening reads back
/// in order, and the pairs a single line cannot answer — how long a room
/// lasted, how long a game took, how many tables a room got through —
/// are a join away. High-cardinality on purpose, so a rollup groups by
/// the other fields and keys on this one only to stitch lines together.
///
/// Every other value is a closed vocabulary or a count, for the same
/// reason one_d4's query events are (QueryEvent.java): the reader keys
/// rollup rows on them, and an unbounded value is a table that grows
/// without bound. Nothing here identifies a player, and no message text,
/// sphere radius or game code appears at all.
///
/// A room id is also a share link. It reaches S3 an hour or more after
/// the fact, by which time an emptied room is gone and the code is dead
/// — but it is the one field here that would let its reader walk into a
/// room that is somehow still open.

/// The event names, the field every reader filters on first. The
/// prefix is the group: otel_contract pins kEvent* against the
/// reader's event names and kOutcome* against its outcomes, and a
/// bare k would make those two lists one.
inline constexpr std::string_view kEventRoomCreated = "room_created";
inline constexpr std::string_view kEventRoomJoined = "room_joined";
inline constexpr std::string_view kEventRoomClosed = "room_closed";
inline constexpr std::string_view kEventChatMessage = "chat_message";
inline constexpr std::string_view kEventGeometryChanged = "geometry_changed";
inline constexpr std::string_view kEventGameStarted = "game_started";
inline constexpr std::string_view kEventGameFinished = "game_finished";

/// The outcomes a game can have. Those are the only two ways one ends.
inline constexpr std::string_view kOutcomeCompleted = "completed";
inline constexpr std::string_view kOutcomeAbandoned = "abandoned";

/// A game that ended, as the event describes it.
struct GameFinished {
  /// "golf" or "castle" — GameKindName's spelling, which is also the
  /// wire's and the stored row's.
  std::string_view variant;
  /// kOutcomeCompleted — the engine played it out — or kOutcomeAbandoned: too few
  /// seats were left to go on.
  std::string_view outcome;
  /// Seats still held at the end. This is NOT the table's size: an
  /// abandoned game ends at the moment the second-to-last seat leaves,
  /// so it reads 1 for almost every abandonment. The table's size is
  /// game_started's `players`, recorded when the table was still whole.
  std::size_t players;
};

/// A room id as it appears in a line: every character outside
/// [A-Za-z0-9_-] replaced, so a line stays text with nothing to escape
/// whatever reaches here. Every id the hub mints already passes through
/// unchanged; this is the guarantee, not a transformation.
std::string RoomTag(std::string_view room);

/// How `state` ended, for a roster of `players` seats.
GameFinished FinishedOf(const HostedState& state, std::size_t players);

/// A room was made, on the surface it chose — SurfaceKindName's
/// spelling, which is the wire's and the stored row's. Its creator is
/// the only one in it, so there is nothing to count yet.
std::string RoomCreatedLine(absl::Time when, std::string_view room, std::string_view surface);
/// A world was reshaped under whoever was standing in it (#1554), onto
/// `surface`. Nothing records the sphere's radius: the question this
/// answers is which shapes people reach for, and a radius is a number
/// per room rather than a word to group by.
std::string GeometryChangedLine(absl::Time when, std::string_view room, std::string_view surface);
/// Somebody walked into a room somebody else had made — `players` is the
/// room's size once they were in it, so 2 is the first one that matters.
/// Creating a room is `room_created`, not a join, and a refused join is
/// no event at all.
std::string RoomJoinedLine(absl::Time when, std::string_view room, std::size_t players);
/// The last member left, so the room is gone — with its games, its chat
/// and its world. Nothing to count: a room closes empty by definition,
/// and what it held is the lines before this one. Paired with
/// `room_created`, the difference over a day is the rooms still open.
std::string RoomClosedLine(absl::Time when, std::string_view room);
/// Somebody said something, and it was stored — a refused message is no
/// event. `players` is the room's size at the time, which is the whole
/// difference between two people talking and a room of four. The text
/// never leaves the process, and nothing here says who spoke.
std::string ChatMessageLine(absl::Time when, std::string_view room, std::size_t players);
/// A table was dealt: which game, and how many seats it was dealt to.
/// The one place the size of a table is recorded while it is still whole.
std::string GameStartedLine(absl::Time when, std::string_view room, std::string_view variant,
                            std::size_t players);
/// A table ended. One JSON object, no newline: the log adds it.
std::string GameFinishedLine(absl::Time when, std::string_view room, const GameFinished& finished);

}  // namespace games_hub

#endif
