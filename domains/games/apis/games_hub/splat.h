#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_SPLAT_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_SPLAT_H

#include <cstdint>

namespace games_hub {

/// Where one tape event lands on a glasshouse's glass (#1554): which wall,
/// and where on it. `u` runs along the wall and `v` up the glass, both a
/// fraction in [0, 1) rather than a distance — that is what keeps the
/// wall's height off the wire and lets the client draw the glass as tall
/// as it likes.
struct Splat {
  std::int32_t wall = 0;
  double u = 0.0;
  double v = 0.0;
};

/// The glass: four walls, numbered 0 (-z), 1 (+x), 2 (+z), 3 (-x).
inline constexpr std::int32_t kGlassWalls = 4;

/// Where `seq` splats. A pure function of deja's sequence number and
/// nothing else, because that number is the only thing every hub instance
/// already agrees on: two instances polling deja separately place the
/// same event on the same spot, so a player sees one wall whichever
/// instance they are on. The answer rides the wire in `TapeSplat`; the
/// client draws what it is handed rather than recomputing this.
Splat SplatFor(std::int64_t seq);

}  // namespace games_hub

#endif
