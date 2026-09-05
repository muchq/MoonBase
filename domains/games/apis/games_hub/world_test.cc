// The world's rules on the World itself (#79, #1490): what a join, move,
// shape, or leave is refused for and that a refusal changes nothing;
// what each admitted command stages, to whom, in what order — the
// joiner's snapshot ahead of anyone's playerJoined, fan-out that stays
// in one room and never echoes. The hub's wiring of it (which room, the
// seat, the socket) is lobby_e2e_test's.

#include "domains/games/apis/games_hub/world.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace games_hub
