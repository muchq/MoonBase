#include "domains/games/libs/pong/rally.h"

#include <algorithm>
#include <cmath>

namespace pong {
namespace {

// smallcat.dog's numbers, per tick at its frame rate.
constexpr double kFlatServe = 2;
constexpr double kFlatSpin = 2;
constexpr double kFlatSpeedUp = 1;
// The tunnel's: deeper, so a faster serve and a gentler climb.
constexpr double kDeepServe = 4;
constexpr double kDeepSpin = 2;
constexpr double kDeepSpeedUp = 0.5;
// The tunnel's serves across the court, in turn.
constexpr Vec2 kDeepLines[] = {{1.5, 1}, {-1.5, 0.75}, {0.75, -1.5}, {-1, -1}};
// The first return speeds the ball up, and every this many after it.
constexpr int kHitsPerSpeedUp = 3;

// The bot's pace, per tick and level, chasing and drifting home.
constexpr double kFlatChase[] = {3, 4, 6};
constexpr double kFlatDrift = 2;
// Close enough on the flat court not to bother.
constexpr double kFlatDeadZone = 3;
constexpr double kDeepChase[] = {2.5, 3.5, 5};
constexpr double kDeepDrift = 1.5;

double Toward(double from, double to, double step) {
  if (to > from) return std::min(to, from + step);
  return std::max(to, from - step);
}

// The range a paddle's centre keeps to on one axis: on court, edge to edge.
double OnCourt(double value, double extent, double paddle) {
  if (extent == 0) return 0;
  return std::clamp(value, paddle / 2, extent - paddle / 2);
}

Vec2 Stepped(Vec2 from, Vec2 target, const Court& court) {
  target.x = OnCourt(target.x, court.width, court.paddle_width);
  target.y = OnCourt(target.y, court.height, court.paddle_height);
  return {Toward(from.x, target.x, court.player_speed),
          Toward(from.y, target.y, court.player_speed)};
}

// Off the walls: back onto the court, heading away from the wall it hit.
void Bounce(double& position, double& velocity, double extent, double radius) {
  if (extent == 0) return;
  if (position > extent - radius) {
    position = extent - radius;
    velocity = -std::abs(velocity);
  } else if (position < radius) {
    position = radius;
    velocity = std::abs(velocity);
  }
}

bool Within(const Rally& rally, const Court& court, int end) {
  const Vec2& paddle = rally.paddles[end];
  if (std::abs(rally.ball.y - paddle.y) > court.paddle_height / 2) return false;
  return court.width == 0 || std::abs(rally.ball.x - paddle.x) <= court.paddle_width / 2;
}

// The ball off `end`'s paddle: back the other way, with spin from where it
// struck, faster on the first return and every third after.
void Return(Rally& rally, const Court& court, int end) {
  const Vec2& paddle = rally.paddles[end];
  rally.velocity.z = -rally.velocity.z;
  rally.ball.z += rally.velocity.z;
  if (rally.variant == Variant::kSmallcat) {
    const double top = paddle.y - court.paddle_height / 2;
    if (rally.ball.y < top + court.paddle_height / 3) {
      rally.velocity.y -= kFlatSpin;
    } else if (rally.ball.y > top + 2 * court.paddle_height / 3) {
      rally.velocity.y += kFlatSpin;
    }
  } else {
    rally.velocity.x += (rally.ball.x - paddle.x) / (court.paddle_width / 2) * kDeepSpin;
    rally.velocity.y += (rally.ball.y - paddle.y) / (court.paddle_height / 2) * kDeepSpin;
  }
  if (++rally.hits % kHitsPerSpeedUp == 1) {
    const double up = rally.variant == Variant::kSmallcat ? kFlatSpeedUp : kDeepSpeedUp;
    rally.velocity.z += rally.velocity.z > 0 ? up : -up;
  }
}

}  // namespace

Court CourtOf(Variant variant) {
  if (variant == Variant::kThreeD) {
    return {.width = 400,
            .height = 300,
            .depth = 800,
            .paddle_width = 100,
            .paddle_height = 75,
            .ball_radius = 12,
            .inset = 10,
            .player_speed = 12};
  }
  return {.width = 0,
          .height = 400,
          .depth = 600,
          .paddle_width = 0,
          .paddle_height = 80,
          .ball_radius = 10,
          .inset = 7,
          .player_speed = 8};
}

std::array<Vec2, kSeats> Centered(Variant variant) {
  const Court court = CourtOf(variant);
  const Vec2 middle{court.width / 2, court.height / 2};
  return {middle, middle};
}

Rally Serve(Variant variant, int receiver, int serve_no) {
  return Serve(variant, receiver, serve_no, Centered(variant));
}

Rally Serve(Variant variant, int receiver, int serve_no, const std::array<Vec2, kSeats>& paddles) {
  const Court court = CourtOf(variant);
  Rally rally;
  rally.variant = variant;
  rally.ball = {court.width / 2, court.height / 2, court.depth / 2};
  rally.paddles = paddles;
  rally.serve_wait = kServeWaitTicks;
  const double toward = receiver == 0 ? -1 : 1;
  if (variant == Variant::kSmallcat) {
    rally.launch = {0, serve_no % 2 == 0 ? kFlatServe : -kFlatServe, toward * kFlatServe};
  } else {
    const Vec2 line = kDeepLines[serve_no % std::size(kDeepLines)];
    rally.launch = {line.x, line.y, toward * kDeepServe};
  }
  return rally;
}

Advanced Advance(const Rally& rally, const std::array<Vec2, kSeats>& targets) {
  const Court court = CourtOf(rally.variant);
  Advanced step{rally, std::nullopt, std::nullopt};
  Rally& next = step.rally;
  for (int end = 0; end < kSeats; ++end) {
    next.paddles[end] = Stepped(next.paddles[end], targets[end], court);
  }
  if (next.serve_wait > 0) {
    if (--next.serve_wait == 0) next.velocity = next.launch;
    return step;
  }
  next.ball.x += next.velocity.x;
  next.ball.y += next.velocity.y;
  next.ball.z += next.velocity.z;
  Bounce(next.ball.x, next.velocity.x, court.width, court.ball_radius);
  Bounce(next.ball.y, next.velocity.y, court.height, court.ball_radius);
  if (next.velocity.z < 0 && next.ball.z - court.ball_radius <= court.inset &&
      Within(next, court, 0)) {
    Return(next, court, 0);
    step.hit_by = 0;
  } else if (next.velocity.z > 0 && next.ball.z + court.ball_radius >= court.depth - court.inset &&
             Within(next, court, 1)) {
    Return(next, court, 1);
    step.hit_by = 1;
  }
  if (next.ball.z < court.ball_radius) {
    step.missed_by = 0;
  } else if (next.ball.z > court.depth - court.ball_radius) {
    step.missed_by = 1;
  }
  return step;
}

Vec2 BotTarget(const Rally& rally, int end, int level) {
  const Court court = CourtOf(rally.variant);
  const int pace = std::clamp(level, kMinBotLevel, kMaxBotLevel) - kMinBotLevel;
  const Vec2& paddle = rally.paddles[end];
  const bool coming = end == 0 ? rally.velocity.z < 0 : rally.velocity.z > 0;
  if (rally.variant == Variant::kSmallcat) {
    if (!coming) return {0, Toward(paddle.y, court.height / 2, kFlatDrift)};
    if (std::abs(rally.ball.y - paddle.y) <= kFlatDeadZone) return paddle;
    return {0, Toward(paddle.y, rally.ball.y, kFlatChase[pace])};
  }
  if (!coming) {
    return {Toward(paddle.x, court.width / 2, kDeepDrift),
            Toward(paddle.y, court.height / 2, kDeepDrift)};
  }
  return {Toward(paddle.x, rally.ball.x, kDeepChase[pace]),
          Toward(paddle.y, rally.ball.y, kDeepChase[pace])};
}

}  // namespace pong
