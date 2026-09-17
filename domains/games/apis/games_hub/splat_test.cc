// Where a tape event lands on the glass (#1554). The hub picks the spot
// from deja's seq alone and sends it, so every client in a room draws the
// same event on the same square inch — this is the only thing that makes
// that true, and the golden block below is the wire it rides on.

#include "domains/games/apis/games_hub/splat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace games_hub {
namespace {

// "wall/u/v" to six places — the comparison a client makes when two
// browsers claim to be looking at the same wall.
std::string Spot(std::int64_t seq) {
  const Splat splat = SplatFor(seq);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%d/%.6f/%.6f", splat.wall, splat.u, splat.v);
  return buffer;
}

TEST(Splat, TheSameSeqAlwaysLandsOnTheSameSpot) {
  for (const std::int64_t seq : {std::int64_t{0}, std::int64_t{1}, std::int64_t{7},
                                 std::int64_t{1'000'003}, std::int64_t{9'007'199'254'740'993}}) {
    EXPECT_EQ(Spot(seq), Spot(seq));
  }
  // Two instances of the hub hold no shared state at all, so a spot that
  // depended on anything but seq would differ here.
  EXPECT_EQ(Spot(42), "1/0.187186/0.932519");
  EXPECT_EQ(Spot(43), "0/0.919188/0.825430");
}

// The golden block: a change to the mixer moves every splat on every
// wall, which is a wire change and has to be a deliberate one.
TEST(Splat, ThePlacementIsPinned) {
  const std::vector<std::string> expected = {
      "1/0.535192/0.532492", "2/0.111684/0.729517", "1/0.855492/0.907602", "2/0.883599/0.451646",
      "2/0.638821/0.094144", "0/0.679441/0.918536", "3/0.348420/0.118638", "2/0.935870/0.948037",
  };
  std::vector<std::string> spots;
  for (std::int64_t seq = 1; seq <= 8; ++seq) spots.push_back(Spot(seq));
  EXPECT_EQ(spots, expected);
}

TEST(Splat, EverySpotIsOnSomeWallAndInsideIt) {
  for (std::int64_t seq = 0; seq < 5000; ++seq) {
    const Splat splat = SplatFor(seq);
    EXPECT_GE(splat.wall, 0) << seq;
    EXPECT_LT(splat.wall, kGlassWalls) << seq;
    EXPECT_GE(splat.u, 0.0) << seq;
    EXPECT_LT(splat.u, 1.0) << seq;
    EXPECT_GE(splat.v, 0.0) << seq;
    EXPECT_LT(splat.v, 1.0) << seq;
  }
}

// A run of events is what the wall actually shows, and a placement that
// walks the glass in a line — or parks a whole minute of traffic on one
// wall — reads as a bug to anyone standing in the room.
TEST(Splat, ConsecutiveEventsScatterOverAllFourWalls) {
  std::set<std::int32_t> walls;
  std::set<std::string> spots;
  for (std::int64_t seq = 1; seq <= 40; ++seq) {
    walls.insert(SplatFor(seq).wall);
    spots.insert(Spot(seq));
  }
  EXPECT_EQ(walls.size(), 4u);
  EXPECT_EQ(spots.size(), 40u) << "forty events piled onto fewer than forty spots";
  // Neighbours land somewhere else entirely, rather than creeping.
  for (std::int64_t seq = 1; seq <= 40; ++seq) {
    EXPECT_NE(Spot(seq), Spot(seq + 1)) << seq;
  }
}

}  // namespace
}  // namespace games_hub
