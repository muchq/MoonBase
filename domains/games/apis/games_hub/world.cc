#include "domains/games/apis/games_hub/world.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace games_hub {

using moonbase::games::LobbyUpdate;

namespace {

// The world's rules, each answering with the reason a client is told.
// NaN fails every comparison, so it is refused by the bounds checks
// themselves rather than by a separate finiteness rule.
std::optional<std::string> PositionProblem(const std::vector<double>& position) {
  if (position.size() != 3) return "position must be [x, y, z]";
  if (!(position[1] == 0.0)) return "y must be 0";
  const double limit = World::kHalfExtent;
  if (!(std::abs(position[0]) <= limit) || !(std::abs(position[2]) <= limit)) {
    return "position out of bounds (±50)";
  }
  return std::nullopt;
}

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
  for (const auto& problem :
       {PositionProblem(join.position), ColorProblem(join.color), ShapeProblem(join.shape)}) {
    if (problem.has_value()) return Refusal{RejectKind::kInvalid, *problem};
  }
  if (world_.contains(player_id)) {
    return Refusal{RejectKind::kState, "already in the world; leave first"};
  }
  Standing standing;
  standing.room_id = room_id;
  standing.player.playerId = player_id;
  standing.player.position = join.position;
  standing.player.color = join.color;
  standing.player.shape = join.shape;

  moonbase::games::WorldState snapshot;
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
  if (const auto problem = PositionProblem(move.position)) {
    return Refusal{RejectKind::kInvalid, *problem};
  }
  const auto it = world_.find(player_id);
  if (it == world_.end()) return Refusal{RejectKind::kState, "join the world first"};
  it->second.player.position = move.position;
  moonbase::games::PlayerMoved moved;
  moved.playerId = player_id;
  moved.position = move.position;
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

}  // namespace games_hub
