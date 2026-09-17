#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_WORLD_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_WORLD_H

#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/surface.h"
#include "moonbase/games/types.h"

namespace games_hub {

/// The lobby's worlds (#79, #1490): every joined player is a position on
/// the room's surface, a color, and a shape, standing in the world of one
/// room; each change fans out to everyone else in the same world and to
/// nobody outside it. No persistence: a world is exactly its players,
/// standing on the surface its room chose (#1554).
///
/// This is the rules and the map, and nothing about wires: it stages
/// what each session is owed, in delivery order, and GolfHub, which
/// hosts it as the room stream's `lobby` member, hands them to its
/// registry under its lock. Not thread-safe; the owner's lock covers
/// every call.
///
/// The rules match the muchq.com world UI's own, so retune them
/// together: position settles on the room's Surface (a flat one's [x, 0,
/// z] within ±kHalfExtent, or a sphere's wall), color is three
/// components in 0..1, shape is 0, 1 or 2. A command that breaks one is
/// refused (kInvalid) and changes nothing, as is move/shape before join
/// (kState). Refused rather than swallowed, so a client can tell a
/// rejected move from a lost one.
class World {
 public:
  static constexpr double kHalfExtent = Surface::kHalfExtent;
  /// The unroomed join's world. Lowercase, so no generated room code
  /// (IdGenerator's uppercase alphanumerics) can name it.
  static constexpr const char* kPlaza = "plaza";

  using Refusal = games_hub::Refusal;
  /// One update owed to one session.
  struct Delivery {
    std::string to;
    moonbase::games::LobbyUpdate update;
  };
  using Deliveries = std::vector<Delivery>;

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

  /// Whether anyone is standing in a world whose surface is a glasshouse
  /// (#1554). The gate on deja's tape and the whole of its cost: false
  /// means the hub sends deja no HTTP at all, and the first joiner and
  /// the last leaver are what flip it.
  [[nodiscard]] bool AnyoneInAGlasshouse() const;

  /// Stages one tape splat to everyone standing in a glasshouse — the
  /// actor-less fan-out, since the event is the world's and nobody sent
  /// it — and remembers it for the next joiner's worldState. Kept
  /// whatever the worlds are doing, so a room that becomes a glasshouse
  /// has something on its glass at once.
  void Splat(const moonbase::games::TapeSplat& splat, Deliveries& out);

  /// How much of the tape a joiner is handed; see WorldState.tape in
  /// lobby.smithy for why it is this and not deja's 200.
  static constexpr std::size_t kTapeMemory = 32;

  /// The surface a room's world stands on; a room never told of is a
  /// plane, the plaza among them. Set when a room is created or adopted,
  /// forgotten when it is deleted.
  void SetSurface(const std::string& room_id, const Surface& surface);
  /// Changes a world's surface under whoever stands in it: each is placed
  /// at the nearest point of the new surface, and everyone in the world,
  /// the actor included, is staged one geometryChanged naming the
  /// surface and every player where they now stand.
  void Reshape(const std::string& room_id, const Surface& surface, Deliveries& out);
  void ForgetSurface(const std::string& room_id);
  Surface SurfaceOf(const std::string& room_id) const;

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
  std::map<std::string, Surface> surfaces_;
  /// The last kTapeMemory splats, oldest first. One ring for the whole
  /// hub rather than one per room: every glasshouse shows the same tape
  /// on the same spots, so a per-room copy would hold identical events
  /// and still leave a room that has just become glass with blank walls.
  std::deque<moonbase::games::TapeSplat> tape_;
};

/// The wire's geometry as a Surface, validated; the refusal names why.
absl::StatusOr<Surface> SurfaceFromGeometry(const moonbase::games::Geometry& geometry);
/// A Surface as the wire spells it.
moonbase::games::Geometry GeometryOf(const Surface& surface);

}  // namespace games_hub

#endif
