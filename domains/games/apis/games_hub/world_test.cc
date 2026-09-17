// The world's rules on the World itself (#79, #1490): what a join, move,
// shape, or leave is refused for and that a refusal changes nothing;
// what each admitted command stages, to whom, in what order — the
// joiner's snapshot ahead of anyone's playerJoined, fan-out that stays
// in one room and never echoes. The hub's wiring of it (which room, the
// seat, the socket) is lobby_e2e_test's.

#include "domains/games/apis/games_hub/world.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"

namespace games_hub {
namespace {

moonbase::games::JoinWorld Join(std::vector<double> position, std::vector<double> color,
                                std::int32_t shape) {
  moonbase::games::JoinWorld join;
  join.position = std::move(position);
  join.color = std::move(color);
  join.shape = shape;
  return join;
}

// The Go server's fixture player: (10, 0, -5), magenta-ish, a sphere.
moonbase::games::JoinWorld FixtureJoin() { return Join({10, 0, -5}, {0.8, 0.2, 0.6}, 0); }

moonbase::games::MoveTo MoveTo(std::vector<double> position) {
  moonbase::games::MoveTo move;
  move.position = std::move(position);
  return move;
}

moonbase::games::ChangeShape Shape(std::int32_t shape) {
  moonbase::games::ChangeShape change;
  change.shape = shape;
  return change;
}

std::string Reason(const std::optional<Refusal>& refusal) {
  return refusal.has_value() ? refusal->reason : "<admitted>";
}

// "<to>:<case>" per delivery, in staged order.
std::vector<std::string> Staged(const World::Deliveries& out) {
  std::vector<std::string> staged;
  for (const auto& delivery : out) {
    staged.push_back(delivery.to + ":" + std::string(delivery.update.case_name()));
  }
  return staged;
}

TEST(World, TheBoundsAreRefusedAsInvalidAndChangeNothing) {
  World world;
  World::Deliveries out;

  struct Case {
    const char* name;
    moonbase::games::JoinWorld join;
    const char* reason;
  };
  const Case refused[] = {
      {"x beyond the edge", Join({100, 0, -5}, {0.8, 0.2, 0.6}, 0), "position out of bounds (±50)"},
      {"z beyond the edge", Join({10, 0, -51}, {0.8, 0.2, 0.6}, 0), "position out of bounds (±50)"},
      {"off the ground plane", Join({10, 1, -5}, {0.8, 0.2, 0.6}, 0), "y must be 0"},
      {"two coordinates", Join({10, 0}, {0.8, 0.2, 0.6}, 0), "position must be [x, y, z]"},
      {"color above 1", Join({10, 0, -5}, {1.5, 0.2, 0.6}, 0),
       "color components must be within 0..1"},
      {"color below 0", Join({10, 0, -5}, {0.8, -0.1, 0.6}, 0),
       "color components must be within 0..1"},
      {"four color components", Join({10, 0, -5}, {0.8, 0.2, 0.6, 1.0}, 0),
       "color must be [r, g, b]"},
      {"negative shape", Join({10, 0, -5}, {0.8, 0.2, 0.6}, -1),
       "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)"},
      {"fourth shape", Join({10, 0, -5}, {0.8, 0.2, 0.6}, 3),
       "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)"},
  };
  for (const auto& c : refused) {
    const auto refusal = world.Join("alice", World::kPlaza, c.join, out);
    ASSERT_TRUE(refusal.has_value()) << c.name;
    EXPECT_EQ(refusal->reason, c.reason) << c.name;
    EXPECT_EQ(refusal->kind, RejectKind::kInvalid) << c.name;
    EXPECT_TRUE(out.empty()) << c.name;
  }

  // The edge itself is a legal place to stand.
  EXPECT_EQ(Reason(world.Join("alice", World::kPlaza, Join({50, 0, -50}, {0, 1, 1}, 2), out)),
            "<admitted>");
  EXPECT_EQ(Staged(out), std::vector<std::string>{"alice:worldState"});
  out.clear();

  // Refused updates are the client's malformed input, and leave the
  // admitted state alone.
  const auto move = world.Move("alice", MoveTo({60, 0, 0}), out);
  ASSERT_TRUE(move.has_value());
  EXPECT_EQ(move->reason, "position out of bounds (±50)");
  EXPECT_EQ(move->kind, RejectKind::kInvalid);
  const auto shape = world.Shape("alice", Shape(5), out);
  ASSERT_TRUE(shape.has_value());
  EXPECT_EQ(shape->reason, "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)");
  EXPECT_EQ(shape->kind, RejectKind::kInvalid);
  EXPECT_TRUE(out.empty());
  ASSERT_FALSE(world.Join("bob", World::kPlaza, FixtureJoin(), out).has_value());
  const auto* snapshot = out.at(0).update.as_worldState_or_null();
  ASSERT_NE(snapshot, nullptr);
  ASSERT_EQ(snapshot->players.size(), 1u);
  EXPECT_EQ(snapshot->players[0].playerId, "alice");
  EXPECT_EQ(snapshot->players[0].position, (std::vector<double>{50, 0, -50}));
  EXPECT_EQ(snapshot->players[0].shape, 2);
}

TEST(World, MoveShapeAndLeaveNeedAJoinAndAJoinNeedsALeave) {
  World world;
  World::Deliveries out;

  for (const auto& refusal :
       {world.Move("alice", MoveTo({1, 0, 1}), out), world.Shape("alice", Shape(1), out)}) {
    ASSERT_TRUE(refusal.has_value());
    EXPECT_EQ(refusal->reason, "join the world first");
    EXPECT_EQ(refusal->kind, RejectKind::kState);
  }
  EXPECT_FALSE(world.Leave("alice", out));
  EXPECT_TRUE(out.empty());

  ASSERT_FALSE(world.Join("alice", World::kPlaza, FixtureJoin(), out).has_value());
  out.clear();
  // Nor is a join elsewhere a way to change rooms: one world at a time.
  for (const auto& room : {std::string(World::kPlaza), std::string("ABC123")}) {
    const auto refusal = world.Join("alice", room, FixtureJoin(), out);
    ASSERT_TRUE(refusal.has_value()) << room;
    EXPECT_EQ(refusal->reason, "already in the world; leave first") << room;
    EXPECT_EQ(refusal->kind, RejectKind::kState) << room;
  }
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(world.Leave("alice", out));
  EXPECT_FALSE(world.Leave("alice", out));
}

TEST(World, FanOutStaysInTheRoomsWorldAndNeverEchoes) {
  World world;
  World::Deliveries out;

  ASSERT_FALSE(world.Join("alice", "R1", FixtureJoin(), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"alice:worldState"});
  out.clear();
  // The joiner's snapshot first, then the room hears of the joiner.
  ASSERT_FALSE(world.Join("bob", "R1", Join({20, 0, 15}, {0.3, 0.9, 0.4}, 1), out).has_value());
  EXPECT_EQ(Staged(out), (std::vector<std::string>{"bob:worldState", "alice:playerJoined"}));
  EXPECT_EQ(out[0].update.as_worldState_or_null()->players.size(), 1u);
  EXPECT_EQ(out[1].update.as_playerJoined_or_null()->player.playerId, "bob");
  out.clear();
  // Another room's world hears nothing of either, nor they of it.
  ASSERT_FALSE(world.Join("carol", World::kPlaza, FixtureJoin(), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"carol:worldState"});
  EXPECT_TRUE(out[0].update.as_worldState_or_null()->players.empty());
  out.clear();

  ASSERT_FALSE(world.Move("alice", MoveTo({3, 0, 3}), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"bob:playerMoved"});
  EXPECT_EQ(out[0].update.as_playerMoved_or_null()->position, (std::vector<double>{3, 0, 3}));
  out.clear();
  ASSERT_FALSE(world.Shape("alice", Shape(2), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"bob:shapeChanged"});
  out.clear();
  // The world remembers: a later joiner's snapshot has alice where she
  // moved, in the shape she took.
  ASSERT_FALSE(world.Join("erin", "R1", FixtureJoin(), out).has_value());
  const auto* seen = out.at(0).update.as_worldState_or_null();
  ASSERT_NE(seen, nullptr);
  ASSERT_EQ(seen->players.size(), 2u);
  EXPECT_EQ(seen->players[0].playerId, "alice");
  EXPECT_EQ(seen->players[0].position, (std::vector<double>{3, 0, 3}));
  EXPECT_EQ(seen->players[0].shape, 2);
  out.clear();
  EXPECT_TRUE(world.Leave("alice", out));
  EXPECT_EQ(Staged(out), (std::vector<std::string>{"bob:playerLeft", "erin:playerLeft"}));
  EXPECT_EQ(out[0].update.as_playerLeft_or_null()->playerId, "alice");
  out.clear();
  // Gone from the snapshot the next joiner gets, and free to rejoin.
  ASSERT_FALSE(world.Join("dave", "R1", FixtureJoin(), out).has_value());
  ASSERT_EQ(out[0].update.as_worldState_or_null()->players.size(), 2u);
  EXPECT_EQ(out[0].update.as_worldState_or_null()->players[0].playerId, "bob");
  EXPECT_EQ(out[0].update.as_worldState_or_null()->players[1].playerId, "erin");
}

// A room told of a sphere settles its world on the wall (#1554): a join
// or move within an avatar's reach lands on the wall, exactly, and that
// is the position everyone hears; one farther off is refused with the
// radius in the reason; the snapshot names the sphere. A room never told
// of is a plane, and stays one for the plane's own rules; a forgotten
// room is a plane again. The plaza is never a sphere: its rules above
// are the plane's.
TEST(World, ASphereRoomSnapsToItsWallRefusesOffItAndNamesItself) {
  World world;
  World::Deliveries out;
  world.SetSurface("S", Surface::Sphere(53));
  EXPECT_EQ(world.SurfaceOf("S"), Surface::Sphere(53));
  EXPECT_EQ(world.SurfaceOf("never told"), Surface::Plane());

  const auto off = world.Join("alice", "S", Join({0, 0, -40}, {0.8, 0.2, 0.6}, 0), out);
  ASSERT_TRUE(off.has_value());
  EXPECT_EQ(off->reason, "position must be on the sphere (radius 53)");
  EXPECT_EQ(off->kind, RejectKind::kInvalid);
  EXPECT_TRUE(out.empty());
  // The plane's y rule does not apply here: the wall is where y is.
  EXPECT_EQ(Reason(world.Join("alice", "S", Join({0, 52.5, 0}, {0.8, 0.2, 0.6}, 0), out)),
            "<admitted>");
  const auto* snapshot = out.at(0).update.as_worldState_or_null();
  ASSERT_NE(snapshot, nullptr);
  ASSERT_NE(snapshot->geometry.as_sphere_or_null(), nullptr);
  EXPECT_EQ(snapshot->geometry.as_sphere_or_null()->radius, 53);
  out.clear();
  EXPECT_EQ(Reason(world.Join("bob", "S", Join({53, 0, 0}, {0.3, 0.9, 0.4}, 1), out)),
            "<admitted>");
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].update.as_worldState_or_null()->players[0].position,
            (std::vector<double>{0, 53, 0}));
  out.clear();

  ASSERT_FALSE(world.Move("alice", MoveTo({0, 0, -52}), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"bob:playerMoved"});
  EXPECT_EQ(out[0].update.as_playerMoved_or_null()->position, (std::vector<double>{0, 0, -53}));
  out.clear();
  const auto far = world.Move("alice", MoveTo({0, 0, -51}), out);
  ASSERT_TRUE(far.has_value());
  EXPECT_EQ(far->reason, "position must be on the sphere (radius 53)");
  EXPECT_TRUE(out.empty());

  // The plaza and every untold room are planes, and say so.
  ASSERT_FALSE(world.Join("carol", World::kPlaza, FixtureJoin(), out).has_value());
  EXPECT_NE(out.at(0).update.as_worldState_or_null()->geometry.as_plane_or_null(), nullptr);
  out.clear();
  world.ForgetSurface("S");
  EXPECT_EQ(world.SurfaceOf("S"), Surface::Plane());
}

// A world changes shape under whoever stands in it (#1554): every
// player is placed at the nearest point of the new surface, everyone in
// that world (the actor is not special: any member may reshape, standing
// or not) hears one geometryChanged carrying the surface and every
// placement, other worlds hear nothing, and the new rules apply to the
// next move. A later joiner's snapshot has the placed positions.
TEST(World, ReshapePlacesEveryoneAndTellsTheWholeWorld) {
  World world;
  World::Deliveries out;
  ASSERT_FALSE(world.Join("alice", "R", Join({10, 0, -5}, {1, 0, 0}, 0), out).has_value());
  ASSERT_FALSE(world.Join("bob", "R", Join({0, 0, 0}, {0, 1, 0}, 1), out).has_value());
  ASSERT_FALSE(world.Join("carol", World::kPlaza, FixtureJoin(), out).has_value());
  out.clear();

  world.Reshape("R", Surface::Sphere(53), out);
  EXPECT_EQ(Staged(out),
            (std::vector<std::string>{"alice:geometryChanged", "bob:geometryChanged"}));
  const auto* changed = out.at(0).update.as_geometryChanged_or_null();
  ASSERT_NE(changed, nullptr);
  ASSERT_NE(changed->geometry.as_sphere_or_null(), nullptr);
  ASSERT_EQ(changed->players.size(), 2u);
  EXPECT_EQ(changed->players[0].playerId, "alice");
  EXPECT_NEAR(std::hypot(changed->players[0].position[0], changed->players[0].position[1],
                         changed->players[0].position[2]),
              53, 1e-9);
  EXPECT_EQ(changed->players[1].playerId, "bob");
  EXPECT_EQ(changed->players[1].position, (std::vector<double>{0, 0, -53}));
  out.clear();

  const auto flat = world.Move("bob", MoveTo({1, 0, 1}), out);
  ASSERT_TRUE(flat.has_value());
  EXPECT_EQ(flat->reason, "position must be on the sphere (radius 53)");
  ASSERT_FALSE(world.Move("bob", MoveTo({0, 0, -52.5}), out).has_value());
  out.clear();
  ASSERT_FALSE(world.Join("dave", "R", Join({53, 0, 0}, {0, 0, 1}, 2), out).has_value());
  const auto* seen = out.at(0).update.as_worldState_or_null();
  ASSERT_NE(seen->geometry.as_sphere_or_null(), nullptr);
  EXPECT_EQ(seen->players[1].position, (std::vector<double>{0, 0, -53}));
  out.clear();

  // Back to the plane: the sphere's positions clamp into its bounds.
  world.Reshape("R", Surface::Plane(), out);
  ASSERT_EQ(out.size(), 3u);
  const auto* back = out.at(2).update.as_geometryChanged_or_null();
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->players[2].position, (std::vector<double>{50, 0, 0}));
  EXPECT_EQ(world.SurfaceOf("R"), Surface::Plane());
}

// The wire's Geometry and the Surface it means, both ways; a sphere too
// small to stand in is refused before it becomes a room.
TEST(World, GeometryAndSurfaceSpellEachOther) {
  const auto plane = SurfaceFromGeometry(GeometryOf(Surface::Plane()));
  ASSERT_TRUE(plane.ok());
  EXPECT_EQ(*plane, Surface::Plane());
  const auto sphere = SurfaceFromGeometry(GeometryOf(Surface::Sphere(53)));
  ASSERT_TRUE(sphere.ok());
  EXPECT_EQ(*sphere, Surface::Sphere(53));

  moonbase::games::SphereGeometry tiny;
  tiny.radius = 1;
  const auto refused = SurfaceFromGeometry(moonbase::games::Geometry::FromSphere(std::move(tiny)));
  EXPECT_EQ(refused.status().message(), "sphere radius must be within 2..1000");

  const auto glass = SurfaceFromGeometry(GeometryOf(Surface::Glasshouse()));
  ASSERT_TRUE(glass.ok());
  EXPECT_EQ(*glass, Surface::Glasshouse());
  // A glasshouse is not a plane on the wire either, or a room that asked
  // for glass would come back flat.
  EXPECT_NE(GeometryOf(Surface::Glasshouse()).as_glasshouse_or_null(), nullptr);
  EXPECT_EQ(GeometryOf(Surface::Glasshouse()).as_plane_or_null(), nullptr);
  EXPECT_EQ(GeometryOf(Surface::Plane()).as_glasshouse_or_null(), nullptr);
}

// The gate on deja's tape (#1554): standing in a glasshouse is what makes
// the hub poll at all, and nothing else does.
TEST(World, OnlyAGlasshouseOccupantOpensTheTape) {
  World world;
  World::Deliveries out;
  world.SetSurface("FLAT", Surface::Plane());
  world.SetSurface("ROUND", Surface::Sphere(53));
  world.SetSurface("GLASS", Surface::Glasshouse());

  // Glass with nobody in it is still no reason to ask deja anything.
  EXPECT_FALSE(world.AnyoneInAGlasshouse());
  ASSERT_FALSE(world.Join("alice", "FLAT", FixtureJoin(), out).has_value());
  ASSERT_FALSE(world.Join("bob", "ROUND", Join({0, 0, -53}, {1, 0, 0}, 0), out).has_value());
  EXPECT_FALSE(world.AnyoneInAGlasshouse()) << "a plane and a sphere have no glass between them";

  // The first joiner opens it, and the last leaver closes it.
  ASSERT_FALSE(world.Join("carol", "GLASS", FixtureJoin(), out).has_value());
  EXPECT_TRUE(world.AnyoneInAGlasshouse());
  ASSERT_FALSE(world.Join("dave", "GLASS", Join({0, 0, 0}, {0, 1, 0}, 1), out).has_value());
  EXPECT_TRUE(world.Leave("carol", out));
  EXPECT_TRUE(world.AnyoneInAGlasshouse()) << "dave is still standing in it";
  EXPECT_TRUE(world.Leave("dave", out));
  EXPECT_FALSE(world.AnyoneInAGlasshouse());

  // Reshaping the room alice stands in opens it under her, with no join.
  world.Reshape("FLAT", Surface::Glasshouse(), out);
  EXPECT_TRUE(world.AnyoneInAGlasshouse());
  world.Reshape("FLAT", Surface::Plane(), out);
  EXPECT_FALSE(world.AnyoneInAGlasshouse());
}

// A splat is the world's, not a player's: everyone standing on glass gets
// it, with no actor to skip, and nobody on a plane or a sphere hears it.
TEST(World, SplatsReachEveryGlasshouseAndNowhereElse) {
  World world;
  World::Deliveries out;
  world.SetSurface("FLAT", Surface::Plane());
  world.SetSurface("GLASS", Surface::Glasshouse());
  world.SetSurface("ATRIUM", Surface::Glasshouse());
  ASSERT_FALSE(world.Join("alice", "FLAT", FixtureJoin(), out).has_value());
  ASSERT_FALSE(world.Join("bob", "GLASS", FixtureJoin(), out).has_value());
  ASSERT_FALSE(world.Join("carol", "GLASS", Join({0, 0, 0}, {0, 1, 0}, 1), out).has_value());
  ASSERT_FALSE(world.Join("dave", "ATRIUM", FixtureJoin(), out).has_value());
  out.clear();

  moonbase::games::TapeSplat splat;
  splat.seq = 42;
  splat.wall = 1;
  splat.actual = "muchq.com GET / 200 browser";
  splat.verdict = "expected";
  world.Splat(splat, out);

  EXPECT_EQ(Staged(out), (std::vector<std::string>{"bob:tape", "carol:tape", "dave:tape"}));
  const auto* fanned = out.at(0).update.as_tape_or_null();
  ASSERT_NE(fanned, nullptr);
  EXPECT_EQ(fanned->seq, 42);
  EXPECT_EQ(fanned->wall, 1);
  EXPECT_EQ(fanned->actual, "muchq.com GET / 200 browser");
}

// A joiner walks into a wall that already has something on it, bounded at
// kTapeMemory and oldest first; a joiner to a plane gets no tape at all.
TEST(World, AJoinerIsHandedTheGlassAsItStands) {
  World world;
  World::Deliveries out;
  world.SetSurface("GLASS", Surface::Glasshouse());
  world.SetSurface("FLAT", Surface::Plane());
  ASSERT_FALSE(world.Join("alice", "GLASS", FixtureJoin(), out).has_value());
  out.clear();

  // Nothing on the glass yet: the member is absent, not an empty list.
  ASSERT_FALSE(world.Join("early", "GLASS", FixtureJoin(), out).has_value());
  EXPECT_FALSE(out.at(0).update.as_worldState_or_null()->tape.has_value());
  ASSERT_TRUE(world.Leave("early", out));
  out.clear();

  const std::size_t splatted = World::kTapeMemory + 5;
  for (std::size_t i = 1; i <= splatted; ++i) {
    moonbase::games::TapeSplat splat;
    splat.seq = static_cast<std::int64_t>(i);
    world.Splat(splat, out);
  }
  out.clear();

  ASSERT_FALSE(world.Join("bob", "GLASS", FixtureJoin(), out).has_value());
  const auto* glass = out.at(0).update.as_worldState_or_null();
  ASSERT_NE(glass, nullptr);
  ASSERT_TRUE(glass->tape.has_value());
  ASSERT_EQ(glass->tape->size(), World::kTapeMemory);
  // Oldest first, and the oldest is the newest minus the ring.
  EXPECT_EQ(glass->tape->front().seq, static_cast<std::int64_t>(splatted - World::kTapeMemory + 1));
  EXPECT_EQ(glass->tape->back().seq, static_cast<std::int64_t>(splatted));
  out.clear();

  // The plane has no glass to hand anyone, however much tape has run.
  ASSERT_FALSE(world.Join("carol", "FLAT", FixtureJoin(), out).has_value());
  EXPECT_FALSE(out.at(0).update.as_worldState_or_null()->tape.has_value());
}

}  // namespace
}  // namespace games_hub
