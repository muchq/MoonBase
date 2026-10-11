#include "domains/games/libs/pong/rally.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace pong {
namespace {

// Holds every paddle where it is.
std::array<Vec2, kSeats> Stay(const Rally& rally) { return rally.paddles; }

// The rally with its ball in flight from `ball` at `velocity`.
Rally InFlight(Variant variant, Vec3 ball, Vec3 velocity) {
  Rally rally = Serve(variant, 1, 0);
  rally.serve_wait = 0;
  rally.ball = ball;
  rally.velocity = velocity;
  return rally;
}

// The serve waits, the ball still at the centre, then heads for the end
// that receives it.
TEST(Rally, TheServeWaitsThenHeadsForTheReceiver) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    const Court court = CourtOf(variant);
    for (const int receiver : {0, 1}) {
      Rally rally = Serve(variant, receiver, 0);
      EXPECT_EQ(rally.serve_wait, kServeWaitTicks);
      for (int tick = 1; tick < kServeWaitTicks; ++tick) {
        rally = Advance(rally, Stay(rally)).rally;
        EXPECT_EQ(rally.ball.z, court.depth / 2);
        EXPECT_EQ(rally.velocity.z, 0);
      }
      rally = Advance(rally, Stay(rally)).rally;
      EXPECT_EQ(rally.serve_wait, 0);
      EXPECT_EQ(rally.velocity.z < 0, receiver == 0) << VariantName(variant);
      EXPECT_NE(rally.velocity.z, 0);
    }
  }
}

// Successive serves do not all take the same line.
TEST(Rally, ServesVaryByPoint) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    const Rally first = Serve(variant, 1, 0);
    const Rally second = Serve(variant, 1, 1);
    EXPECT_FALSE(first.launch.x == second.launch.x && first.launch.y == second.launch.y)
        << VariantName(variant);
  }
}

// A paddle chases its target at the variant's speed, and never leaves the
// court however far the target lies.
TEST(Rally, APaddleChasesItsTargetAtItsSpeedAndStaysOnCourt) {
  const Court flat = CourtOf(Variant::kSmallcat);
  Rally rally = Serve(Variant::kSmallcat, 1, 0);
  const double from = rally.paddles[0].y;
  rally = Advance(rally, {Vec2{0, 10'000}, rally.paddles[1]}).rally;
  EXPECT_EQ(rally.paddles[0].y, from + flat.player_speed);
  for (int i = 0; i < 100; ++i) rally = Advance(rally, {Vec2{0, 10'000}, rally.paddles[1]}).rally;
  EXPECT_EQ(rally.paddles[0].y, flat.height - flat.paddle_height / 2);
  rally = Advance(rally, {Vec2{0, rally.paddles[0].y - 1}, rally.paddles[1]}).rally;
  EXPECT_EQ(rally.paddles[0].y, flat.height - flat.paddle_height / 2 - 1) << "short of a full step";
  EXPECT_EQ(rally.paddles[0].x, 0) << "the flat court has no width to move in";

  const Court deep = CourtOf(Variant::kThreeD);
  Rally tunnel = Serve(Variant::kThreeD, 1, 0);
  for (int i = 0; i < 200; ++i)
    tunnel = Advance(tunnel, {Vec2{-500, -500}, tunnel.paddles[1]}).rally;
  EXPECT_EQ(tunnel.paddles[0].x, deep.paddle_width / 2);
  EXPECT_EQ(tunnel.paddles[0].y, deep.paddle_height / 2);
}

TEST(Rally, APaddleInTheBallsWayReturnsIt) {
  const Court court = CourtOf(Variant::kSmallcat);
  const double y = court.height / 2;
  Rally rally = InFlight(Variant::kSmallcat,
                         {0, y, court.depth - court.inset - court.ball_radius - 1}, {0, 0, 2});
  rally.paddles[1].y = y;
  const Advanced step = Advance(rally, Stay(rally));
  EXPECT_EQ(step.hit_by, 1);
  EXPECT_EQ(step.missed_by, std::nullopt);
  EXPECT_LT(step.rally.velocity.z, 0);
  EXPECT_EQ(step.rally.hits, 1);
}

// A paddle returns what strikes its face, edge to edge, and nothing wider.
TEST(Rally, APaddleReachesToItsEdgesAndNoFurther) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    const Court court = CourtOf(variant);
    const auto returned = [&](Vec2 offset) {
      const Vec2 middle{court.width / 2, court.height / 2};
      Rally rally = InFlight(variant,
                             {middle.x + offset.x, middle.y + offset.y,
                              court.depth - court.inset - court.ball_radius - 1},
                             {0, 0, 2});
      rally.paddles[1] = middle;
      return Advance(rally, Stay(rally)).hit_by.has_value();
    };
    EXPECT_TRUE(returned({0, court.paddle_height / 2 - 1})) << VariantName(variant);
    EXPECT_FALSE(returned({0, court.paddle_height / 2 + 1})) << VariantName(variant);
    if (court.width > 0) {
      EXPECT_TRUE(returned({court.paddle_width / 2 - 1, 0}));
      EXPECT_FALSE(returned({court.paddle_width / 2 + 1, 0}));
    }
  }
}

TEST(Rally, ABallPastThePaddleIsAMissForThatEnd) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    const Court court = CourtOf(variant);
    Rally rally =
        InFlight(variant, {court.width / 2, court.height / 2, court.ball_radius + 1}, {0, 0, -4});
    // Its paddle in the far corner.
    rally.paddles[0] = {court.width == 0 ? 0 : court.paddle_width / 2, court.paddle_height / 2};
    std::optional<int> missed;
    for (int tick = 0; tick < 5 && !missed; ++tick) {
      const Advanced step = Advance(rally, Stay(rally));
      EXPECT_EQ(step.hit_by, std::nullopt);
      missed = step.missed_by;
      rally = step.rally;
    }
    EXPECT_EQ(missed, 0) << VariantName(variant);
  }
}

TEST(Rally, TheBallBouncesOffTheWalls) {
  const Court flat = CourtOf(Variant::kSmallcat);
  Rally rally = InFlight(Variant::kSmallcat,
                         {0, flat.height - flat.ball_radius - 1, flat.depth / 2}, {0, 3, 2});
  rally = Advance(rally, Stay(rally)).rally;
  EXPECT_LT(rally.velocity.y, 0);
  EXPECT_LE(rally.ball.y, flat.height - flat.ball_radius);

  const Court deep = CourtOf(Variant::kThreeD);
  Rally tunnel = InFlight(Variant::kThreeD, {deep.ball_radius + 1, deep.height / 2, deep.depth / 2},
                          {-3, 0, 2});
  tunnel = Advance(tunnel, Stay(tunnel)).rally;
  EXPECT_GT(tunnel.velocity.x, 0);
  EXPECT_GE(tunnel.ball.x, deep.ball_radius);
}

// smallcat's english: off the paddle's top third the ball climbs, off its
// bottom third it falls, off the middle it keeps its line.
TEST(Rally, OffAFlatPaddlesEdgeTheBallTakesSpin) {
  const Court court = CourtOf(Variant::kSmallcat);
  const double paddle = court.height / 2;
  const auto spin_off = [&](double offset) {
    Rally rally = InFlight(Variant::kSmallcat,
                           {0, paddle + offset, court.depth - court.inset - court.ball_radius - 1},
                           {0, 0, 2});
    rally.paddles[1].y = paddle;
    return Advance(rally, Stay(rally)).rally.velocity.y;
  };
  EXPECT_LT(spin_off(-court.paddle_height / 2 + 2), 0);
  EXPECT_EQ(spin_off(-court.paddle_height / 6 + 2), 0);
  EXPECT_EQ(spin_off(0), 0);
  EXPECT_EQ(spin_off(court.paddle_height / 6 - 2), 0);
  EXPECT_GT(spin_off(court.paddle_height / 2 - 2), 0);
}

// In the tunnel the ball leaves a paddle at an angle as far off its
// middle as it struck, on both axes.
TEST(Rally, OffATunnelPaddleTheBallAnglesAwayFromItsMiddle) {
  const Court court = CourtOf(Variant::kThreeD);
  Rally rally =
      InFlight(Variant::kThreeD,
               {court.width / 2 + 30, court.height / 2 - 20, court.inset + court.ball_radius + 1},
               {0, 0, -3});
  rally.paddles[0] = {court.width / 2, court.height / 2};
  const Advanced step = Advance(rally, Stay(rally));
  ASSERT_EQ(step.hit_by, 0);
  EXPECT_GT(step.rally.velocity.x, 0);
  EXPECT_LT(step.rally.velocity.y, 0);
  EXPECT_GT(step.rally.velocity.z, 0);
}

// smallcat.dog's speed-up: the first return, then every third after it.
TEST(Rally, TheFirstReturnAndEveryThirdAfterAreFaster) {
  const Court court = CourtOf(Variant::kSmallcat);
  const double y = court.height / 2;
  Rally rally = InFlight(Variant::kSmallcat, {0, y, court.depth / 2}, {0, 0, 2});
  rally.paddles = {Vec2{0, y}, Vec2{0, y}};
  std::vector<double> speeds;
  for (int tick = 0; tick < 10'000 && speeds.size() < 4; ++tick) {
    const Advanced step = Advance(rally, Stay(rally));
    ASSERT_EQ(step.missed_by, std::nullopt);
    rally = step.rally;
    if (step.hit_by.has_value()) speeds.push_back(std::abs(rally.velocity.z));
  }
  ASSERT_EQ(speeds.size(), 4u);
  EXPECT_EQ(speeds[0], 3);
  EXPECT_EQ(speeds[1], 3);
  EXPECT_EQ(speeds[2], 3);
  EXPECT_EQ(speeds[3], 4);
}

// The bot chases a ball coming its way no faster than its level allows,
// and a higher level is quicker.
TEST(Bot, ChasesABallComingItsWayAtItsLevelsPace) {
  const Court court = CourtOf(Variant::kSmallcat);
  const Rally rally =
      InFlight(Variant::kSmallcat, {0, court.height - 20, court.depth / 2}, {0, 0, 2});
  double previous = 0;
  for (int level = kMinBotLevel; level <= kMaxBotLevel; ++level) {
    const Vec2 target = BotTarget(rally, 1, level);
    const double moved = target.y - rally.paddles[1].y;
    EXPECT_GT(moved, previous) << "level " << level;
    EXPECT_LE(moved, court.player_speed) << "no faster than a person";
    previous = moved;
  }
}

// With the ball going away it drifts back toward the middle, and holds
// there.
TEST(Bot, DriftsToTheMiddleWhileTheBallIsAway) {
  const Court court = CourtOf(Variant::kThreeD);
  Rally rally = InFlight(Variant::kThreeD, {50, 50, court.depth / 2}, {0, 0, 2});
  rally.paddles[0] = {court.paddle_width / 2, court.paddle_height / 2};
  const Vec2 target = BotTarget(rally, 0, 2);
  EXPECT_GT(target.x, rally.paddles[0].x);
  EXPECT_GT(target.y, rally.paddles[0].y);
  rally.paddles[0] = {court.width / 2, court.height / 2};
  const Vec2 held = BotTarget(rally, 0, 2);
  EXPECT_EQ(held.x, court.width / 2);
  EXPECT_EQ(held.y, court.height / 2);
}

// Two bots play for as long as they like and the ball never leaves the
// court: every rally ends on a miss at one end or the other.
TEST(Bot, TwoBotsKeepTheBallOnCourt) {
  for (const Variant variant : {Variant::kSmallcat, Variant::kThreeD}) {
    const Court court = CourtOf(variant);
    int points = 0;
    Rally rally = Serve(variant, 1, 0);
    for (int tick = 0; tick < 200'000 && points < 6; ++tick) {
      const Advanced step =
          Advance(rally, {BotTarget(rally, 0, 1 + points % 3), BotTarget(rally, 1, 3)});
      rally = step.rally;
      ASSERT_GE(rally.ball.y, 0) << VariantName(variant);
      ASSERT_LE(rally.ball.y, court.height);
      ASSERT_GE(rally.ball.x, 0);
      ASSERT_LE(rally.ball.x, court.width);
      if (step.missed_by.has_value()) {
        ++points;
        rally = Serve(variant, *step.missed_by, points, rally.paddles);
      }
    }
    EXPECT_EQ(points, 6) << VariantName(variant) << ": rallies should end";
  }
}

}  // namespace
}  // namespace pong
