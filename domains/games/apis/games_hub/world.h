#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WORLD_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WORLD_H

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"
#include "moonbase/games/types.h"

namespace games_hub {

/// The thoughts worlds (#79, #1490): every joined player is a position on
/// the ground plane, a color, and a shape, standing in the world of one
/// room; each change fans out to everyone else in the same world and to
/// nobody outside it. No persistence: a world is exactly its players.
///
/// This is the rules and the map, and nothing about wires: it stages
/// what each session is owed, in delivery order, and the hub that owns
/// it — ThoughtsHub on the Think stream, GolfHub as the room stream's
/// `lobby` member — queues them on its own registry under its own
/// ordering rule (ThoughtsHub under its lock, GolfHub through its
/// Outbox). Not thread-safe; the owner's lock covers every call.
///
/// The rules match the muchq.com/thoughts UI's own bounds, so retune
/// them together: position is [x, 0, z] with x and z within
/// ±kHalfExtent, color is three components in 0..1, shape is 0, 1 or 2.
/// A command that breaks one is refused (kInvalid) and changes nothing,
/// as is move/shape before join (kState). Refused rather than swallowed,
/// so a client can tell a rejected move from a lost one.
class World {
 public:
  static constexpr double kHalfExtent = 50.0;
  /// The unroomed join's world. Lowercase, so no generated room code
  /// (IdGenerator's uppercase alphanumerics) can name it.
  static constexpr const char* kPlaza = "plaza";
  /// A room id is a client string this world retains and compares on
  /// every frame; the bound keeps both costs the hub's to choose, with
  /// room to spare over IdGenerator's six-character codes.
  static constexpr std::size_t kMaxRoomIdLength = 64;

  using Refusal = games_hub::Refusal;
  /// One update owed to one session.
  struct Delivery {
    std::string to;
    moonbase::games::LobbyUpdate update;
  };
  using Deliveries = std::vector<Delivery>;

  /// A named room, when a stream lets the client name one: golf's NUL
  /// rule and its reason, plus an empty id refused and a length bound,
  /// because any id here creates a world and no store is behind it to
  /// refuse one.
  static std::optional<std::string> RoomProblem(const std::optional<std::string>& room_id);

  /// Enters `room_id`'s world. Stages the joiner's snapshot of that world
  /// first, then playerJoined to the rest of it, so whatever reaches the
  /// joiner after the snapshot happened after it. Refused while already
  /// in a world (leave first: that is how a color or a room changes), or
  /// for a value outside the rules; a refusal stages nothing.
  std::optional<Refusal> Join(const std::string& player_id, const std::string& room_id,
                              const moonbase::games::JoinWorld& join, Deliveries& out);
  std::optional<Refusal> Move(const std::string& player_id, const moonbase::games::MoveTo& move,
                              Deliveries& out);
  std::optional<Refusal> Shape(const std::string& player_id,
                               const moonbase::games::ChangeShape& shape, Deliveries& out);
  /// Removes the player from their world and stages playerLeft to the
  /// rest of it; false when they were in none. A deliberate leave and a
  /// closed socket alike.
  bool Leave(const std::string& player_id, Deliveries& out);

 private:
  struct Standing {
    std::string room_id;
    moonbase::games::WorldPlayer player;
  };
  void FanOut(const std::string& room_id, const std::string& actor_id,
              const moonbase::games::LobbyUpdate& update, Deliveries& out) const;

  /// Every joined player by id, with the room whose world they stand in.
  /// One map rather than one per world, so there is no world lifecycle
  /// to manage; a fan-out scans every joined player, not only the
  /// room's — fine at the tens of players this hub sees.
  std::map<std::string, Standing> world_;
};

}  // namespace games_hub

#endif
