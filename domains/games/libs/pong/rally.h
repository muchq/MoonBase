#ifndef DOMAINS_GAMES_LIBS_PONG_RALLY_H
#define DOMAINS_GAMES_LIBS_PONG_RALLY_H

#include <array>
#include <optional>

#include "domains/games/libs/pong/table.h"

namespace pong {

/// A rally ticks this often: smallcat.dog's frame rate, whose speeds the
/// flat court keeps.
inline constexpr int kTicksPerSecond = 60;
/// How long the ball sits at the centre before it is served.
inline constexpr int kServeWaitTicks = kTicksPerSecond;
inline constexpr int kMinBotLevel = 1;
inline constexpr int kMaxBotLevel = 3;

struct Vec2 {
  double x = 0;
  double y = 0;
  bool operator==(const Vec2&) const = default;
};

struct Vec3 {
  double x = 0;
  double y = 0;
  double z = 0;
  bool operator==(const Vec3&) const = default;
};

/// The court in its own units. The ball travels along z, from end 0 at
/// z = 0 to end 1 at z = depth; a paddle moves across x and y on its end.
/// The flat court has no width, so its paddles and ball move in y alone.
struct Court {
  double width = 0;
  double height = 0;
  double depth = 0;
  double paddle_width = 0;
  double paddle_height = 0;
  double ball_radius = 0;
  /// How far in from its end a paddle's face stands.
  double inset = 0;
  /// The farthest a paddle moves in a tick, on each axis.
  double player_speed = 0;
};
[[nodiscard]] Court CourtOf(Variant variant);

/// One point's play, between serve and miss: the motion a table never
/// holds. Paddles are centres.
struct Rally {
  Variant variant = Variant::kSmallcat;
  Vec3 ball;
  Vec3 velocity;
  std::array<Vec2, kSeats> paddles;
  /// The ball's velocity the moment it is served.
  Vec3 launch;
  /// Paddle returns this rally.
  int hits = 0;
  /// Ticks before the serve; 0 once the ball is in flight.
  int serve_wait = 0;
};

/// The paddles at rest in the middle of their ends.
[[nodiscard]] std::array<Vec2, kSeats> Centered(Variant variant);

/// The ball at the centre, waiting kServeWaitTicks to head for
/// `receiver`'s end. `serve_no` picks its line, so serves vary point to
/// point and the same point always serves the same way.
[[nodiscard]] Rally Serve(Variant variant, int receiver, int serve_no);
[[nodiscard]] Rally Serve(Variant variant, int receiver, int serve_no,
                          const std::array<Vec2, kSeats>& paddles);

struct Advanced {
  Rally rally;
  /// The end the ball got past this tick: the point is over.
  std::optional<int> missed_by;
  /// The end whose paddle returned the ball this tick.
  std::optional<int> hit_by;
};

/// One tick: each paddle steps toward its target, no farther than the
/// court's player speed and never off its end; then the ball moves,
/// bounces off the walls, comes back off a paddle in its way, or gets
/// past one. smallcat's rules on the flat court: spin off a paddle's top
/// or bottom third, and faster on the first return and every third after.
[[nodiscard]] Advanced Advance(const Rally& rally, const std::array<Vec2, kSeats>& targets);

/// Where the bot at `end` moves its paddle this tick, at `level`
/// (kMinBotLevel..kMaxBotLevel): smallcat's own opponent, which chases a
/// ball coming its way and drifts back to the middle while it goes away.
/// The target is a step away at most, so Advance moves it there.
[[nodiscard]] Vec2 BotTarget(const Rally& rally, int end, int level);

}  // namespace pong

#endif  // DOMAINS_GAMES_LIBS_PONG_RALLY_H
