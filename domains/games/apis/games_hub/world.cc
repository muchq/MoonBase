#include "domains/games/apis/games_hub/world.h"

#include <cstdint>
#include <utility>

#include "absl/status/status.h"

namespace games_hub {

using moonbase::games::LobbyUpdate;

namespace {

// The world's rules, each answering with the reason a client is told;
// the position's is the room's Surface. NaN fails every comparison, so
// it is refused by the checks themselves rather than by a separate
// finiteness rule.
std::optional<std::string> ColorProblem(const std::vector<double>& color) {
  if (color.size() != 3) return "color must be [r, g, b]";
  for (const double component : color) {
    if (!(component >= 0.0 && component <= 1.0)) return "color components must be within 0..1";
  }
  return std::nullopt;
}

std::optional<std::string> ShapeProblem(std::int32_t shape) {
  if (shape < 0 || shape > 2) return "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)";
  return std::nullopt;
}

}  // namespace

std::optional<World::Refusal> World::Join(const std::string& player_id, const std::string& room_id,
                                          const moonbase::games::JoinWorld& join, Deliveries& out) {
  std::vector<double> position = join.position;
  const Surface surface = SurfaceOf(room_id);
  for (const auto& problem :
       {surface.Settle(position), ColorProblem(join.color), ShapeProblem(join.shape)}) {
    if (problem.has_value()) return Refusal{RejectKind::kInvalid, *problem};
  }
  if (world_.contains(player_id)) {
    return Refusal{RejectKind::kState, "already in the world; leave first"};
  }
  Standing standing;
  standing.room_id = room_id;
  standing.player.playerId = player_id;
  standing.player.position = std::move(position);
  standing.player.color = join.color;
  standing.player.shape = join.shape;

  moonbase::games::WorldState snapshot;
  snapshot.geometry = GeometryOf(surface);
  for (const auto& [id, other] : world_) {
    if (other.room_id == room_id) snapshot.players.push_back(other.player);
  }
  out.push_back({player_id, LobbyUpdate::FromWorldstate(std::move(snapshot))});
  moonbase::games::PlayerJoined joined;
  joined.player = standing.player;
  FanOut(room_id, player_id, LobbyUpdate::FromPlayerjoined(std::move(joined)), out);
  world_.emplace(player_id, std::move(standing));
  return std::nullopt;
}

std::optional<World::Refusal> World::Move(const std::string& player_id,
                                          const moonbase::games::MoveTo& move, Deliveries& out) {
  // The surface is the room's, so who is moving comes first.
  const auto it = world_.find(player_id);
  if (it == world_.end()) return Refusal{RejectKind::kState, "join the world first"};
  std::vector<double> position = move.position;
  if (const auto problem = SurfaceOf(it->second.room_id).Settle(position)) {
    return Refusal{RejectKind::kInvalid, *problem};
  }
  it->second.player.position = position;
  moonbase::games::PlayerMoved moved;
  moved.playerId = player_id;
  moved.position = std::move(position);
  FanOut(it->second.room_id, player_id, LobbyUpdate::FromPlayermoved(std::move(moved)), out);
  return std::nullopt;
}

std::optional<World::Refusal> World::Shape(const std::string& player_id,
                                           const moonbase::games::ChangeShape& shape,
                                           Deliveries& out) {
  if (const auto problem = ShapeProblem(shape.shape)) {
    return Refusal{RejectKind::kInvalid, *problem};
  }
  const auto it = world_.find(player_id);
  if (it == world_.end()) return Refusal{RejectKind::kState, "join the world first"};
  it->second.player.shape = shape.shape;
  moonbase::games::ShapeChanged changed;
  changed.playerId = player_id;
  changed.shape = shape.shape;
  FanOut(it->second.room_id, player_id, LobbyUpdate::FromShapechanged(std::move(changed)), out);
  return std::nullopt;
}

bool World::Leave(const std::string& player_id, Deliveries& out) {
  const auto it = world_.find(player_id);
  if (it == world_.end()) return false;
  const std::string room_id = std::move(it->second.room_id);
  world_.erase(it);
  moonbase::games::PlayerLeft left;
  left.playerId = player_id;
  FanOut(room_id, player_id, LobbyUpdate::FromPlayerleft(std::move(left)), out);
  return true;
}

void World::FanOut(const std::string& room_id, const std::string& actor_id,
                   const LobbyUpdate& update, Deliveries& out) const {
  for (const auto& [id, standing] : world_) {
    if (id != actor_id && standing.room_id == room_id) out.push_back({id, update});
  }
}

void World::SetSurface(const std::string& room_id, const Surface& surface) {
  surfaces_[room_id] = surface;
}

void World::ForgetSurface(const std::string& room_id) { surfaces_.erase(room_id); }

Surface World::SurfaceOf(const std::string& room_id) const {
  const auto it = surfaces_.find(room_id);
  return it != surfaces_.end() ? it->second : Surface::Plane();
}

absl::StatusOr<Surface> SurfaceFromGeometry(const moonbase::games::Geometry& geometry) {
  if (geometry.as_plane_or_null() != nullptr) return Surface::Plane();
  if (const auto* sphere = geometry.as_sphere_or_null()) {
    if (const auto problem = Surface::RadiusProblem(sphere->radius)) {
      return absl::InvalidArgumentError(*problem);
    }
    return Surface::Sphere(sphere->radius);
  }
  return absl::InvalidArgumentError("geometry must be plane or sphere");
}

moonbase::games::Geometry GeometryOf(const Surface& surface) {
  if (surface.kind == Surface::Kind::kSphere) {
    moonbase::games::SphereGeometry sphere;
    sphere.radius = surface.radius;
    return moonbase::games::Geometry::FromSphere(std::move(sphere));
  }
  return moonbase::games::Geometry::FromPlane(moonbase::games::PlaneGeometry{});
}

}  // namespace games_hub
