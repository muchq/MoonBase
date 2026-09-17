// A room's surface: what it refuses, what it settles, and how it is
// stored. The plane keeps the lobby's original rules to the letter; the
// sphere puts a near-enough position on its wall and refuses the rest;
// the glasshouse is the plane's floor inside glass that is not standable.

#include "domains/games/apis/games_hub/surface.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace games_hub {
namespace {

std::string Settled(const Surface& surface, std::vector<double> position) {
  const auto problem = surface.Settle(position);
  return problem.has_value() ? *problem : "<settled>";
}

TEST(Surface, ThePlaneKeepsTheLobbysOriginalRules) {
  const Surface plane = Surface::Plane();
  EXPECT_EQ(Settled(plane, {10, 0, -5}), "<settled>");
  EXPECT_EQ(Settled(plane, {50, 0, -50}), "<settled>");
  EXPECT_EQ(Settled(plane, {100, 0, -5}), "position out of bounds (±50)");
  EXPECT_EQ(Settled(plane, {10, 0, -51}), "position out of bounds (±50)");
  EXPECT_EQ(Settled(plane, {10, 1, -5}), "y must be 0");
  EXPECT_EQ(Settled(plane, {10, 0}), "position must be [x, y, z]");
  EXPECT_EQ(Settled(plane, {std::nan(""), 0, 0}), "position out of bounds (±50)");
  // The plane takes a position exactly as it came.
  std::vector<double> position = {49.5, 0, -0.25};
  EXPECT_FALSE(plane.Settle(position).has_value());
  EXPECT_EQ(position, (std::vector<double>{49.5, 0, -0.25}));
}

TEST(Surface, TheSphereSnapsNearTheWallAndRefusesTheRest) {
  const Surface sphere = Surface::Sphere(53);
  // On the wall: unchanged.
  std::vector<double> on = {0, 0, -53};
  EXPECT_FALSE(sphere.Settle(on).has_value());
  EXPECT_EQ(on, (std::vector<double>{0, 0, -53}));
  // A tangent step lands just inside: snapped back out along the radius.
  std::vector<double> inside = {0, 0, -52.5};
  EXPECT_FALSE(sphere.Settle(inside).has_value());
  EXPECT_NEAR(inside[2], -53, 1e-9);
  std::vector<double> slanted = {30.3, 30.3, 30.3};  // length 52.48
  EXPECT_FALSE(sphere.Settle(slanted).has_value());
  EXPECT_NEAR(std::hypot(slanted[0], slanted[1], slanted[2]), 53, 1e-9);
  EXPECT_NEAR(slanted[0], slanted[1], 1e-9);
  // Off by more than an avatar, either way, is malformed.
  EXPECT_EQ(Settled(sphere, {0, 0, -51.9}), "position must be on the sphere (radius 53)");
  EXPECT_EQ(Settled(sphere, {0, 0, -54.1}), "position must be on the sphere (radius 53)");
  EXPECT_EQ(Settled(sphere, {0, 0, 0}), "position must be on the sphere (radius 53)");
  EXPECT_EQ(Settled(sphere, {0, 0, std::nan("")}), "position must be on the sphere (radius 53)");
  EXPECT_EQ(Settled(sphere, {0, 0, std::numeric_limits<double>::infinity()}),
            "position must be on the sphere (radius 53)");
  EXPECT_EQ(Settled(sphere, {0, -53}), "position must be [x, y, z]");
  // Inside a plane's bounds is no help on a sphere.
  EXPECT_EQ(Settled(sphere, {10, 0, -5}), "position must be on the sphere (radius 53)");
}

// Where a player lands when their world changes shape: the nearest
// point of the new surface, always a settled one.
TEST(Surface, PlacePutsAnyPointOnTheSurface) {
  const Surface plane = Surface::Plane();
  EXPECT_EQ(plane.Place({10, 0, -5}), (std::vector<double>{10, 0, -5}));
  EXPECT_EQ(plane.Place({0, 53, 0}), (std::vector<double>{0, 0, 0}));
  EXPECT_EQ(plane.Place({53, 0, -53}), (std::vector<double>{50, 0, -50}));
  const Surface sphere = Surface::Sphere(53);
  EXPECT_EQ(sphere.Place({0, 0, -53}), (std::vector<double>{0, 0, -53}));
  EXPECT_EQ(sphere.Place({0, 0, -5}), (std::vector<double>{0, 0, -53}));
  EXPECT_EQ(sphere.Place({0, 0, 0}), (std::vector<double>{0, 0, -53}));
  std::vector<double> placed = sphere.Place({30, 30, 30});
  EXPECT_NEAR(std::hypot(placed[0], placed[1], placed[2]), 53, 1e-9);
  EXPECT_NEAR(placed[0], placed[1], 1e-9);
  for (const auto& surface : {plane, sphere}) {
    for (std::vector<double> from :
         {std::vector<double>{10, 0, -5}, std::vector<double>{0, 53, 0}}) {
      std::vector<double> settled = surface.Place(from);
      EXPECT_FALSE(surface.Settle(settled).has_value());
    }
  }
}

// The glasshouse (#1554) is the plane's floor inside four glass walls at
// the edges it already had: the same positions settle, the same ones are
// refused, and the glass is not somewhere to stand — a position off the
// floor is refused exactly as it is on the plane, however tall the walls
// are drawn.
TEST(Surface, TheGlasshouseFloorIsThePlanesAndTheGlassIsNotStandable) {
  const Surface glass = Surface::Glasshouse();
  const Surface plane = Surface::Plane();
  for (const std::vector<double>& at :
       {std::vector<double>{10, 0, -5}, std::vector<double>{50, 0, -50},
        std::vector<double>{100, 0, -5}, std::vector<double>{10, 0, -51},
        std::vector<double>{10, 1, -5}, std::vector<double>{-50, 0, 50},
        std::vector<double>{std::nan(""), 0, 0}}) {
    EXPECT_EQ(Settled(glass, at), Settled(plane, at));
    EXPECT_EQ(glass.Place(at), plane.Place(at));
  }
  EXPECT_EQ(Settled(glass, {10, 0}), "position must be [x, y, z]");
  // Standing halfway up the glass is standing on nothing.
  EXPECT_EQ(Settled(glass, {50, 12, 0}), "y must be 0");
  // Against the glass, on the floor, is a legal place to stand.
  EXPECT_EQ(Settled(glass, {50, 0, 17}), "<settled>");

  // Only the glasshouse has glass; that flag is the poller's whole gate.
  EXPECT_TRUE(glass.has_glass());
  EXPECT_FALSE(plane.has_glass());
  EXPECT_FALSE(Surface::Sphere(53).has_glass());
  EXPECT_TRUE(glass.is_flat());
  EXPECT_FALSE(Surface::Sphere(53).is_flat());
  // And it is a different surface from the plane, or reshaping to it
  // would announce nothing.
  EXPECT_NE(glass, plane);
}

TEST(Surface, ARadiusMustLeaveRoomToStand) {
  EXPECT_FALSE(Surface::RadiusProblem(2).has_value());
  EXPECT_FALSE(Surface::RadiusProblem(53).has_value());
  EXPECT_FALSE(Surface::RadiusProblem(1000).has_value());
  EXPECT_EQ(Surface::RadiusProblem(1000.01), "sphere radius must be within 2..1000");
  EXPECT_EQ(Surface::RadiusProblem(1.99), "sphere radius must be within 2..1000");
  EXPECT_EQ(Surface::RadiusProblem(0), "sphere radius must be within 2..1000");
  EXPECT_EQ(Surface::RadiusProblem(-53), "sphere radius must be within 2..1000");
  EXPECT_EQ(Surface::RadiusProblem(std::nan("")), "sphere radius must be within 2..1000");
  EXPECT_EQ(Surface::RadiusProblem(std::numeric_limits<double>::infinity()),
            "sphere radius must be within 2..1000");
}

TEST(Surface, StoredFormRoundTripsAndRefusesWhatItCannotRead) {
  EXPECT_EQ(SurfaceJson(Surface::Plane()), R"({"plane":{}})");
  EXPECT_EQ(SurfaceJson(Surface::Sphere(53)), R"({"sphere":{"radius":53.0}})");
  EXPECT_EQ(SurfaceJson(Surface::Glasshouse()), R"({"glasshouse":{}})");
  for (const Surface& surface :
       {Surface::Plane(), Surface::Sphere(53), Surface::Sphere(2.5), Surface::Glasshouse()}) {
    const auto back = SurfaceFromJson(SurfaceJson(surface));
    ASSERT_TRUE(back.ok()) << back.status();
    EXPECT_EQ(*back, surface);
  }
  // Postgres re-spaces jsonb; the reader does not care.
  auto spaced = SurfaceFromJson(R"({"sphere": {"radius": 53}})");
  ASSERT_TRUE(spaced.ok());
  EXPECT_EQ(*spaced, Surface::Sphere(53));
  for (const char* bad : {"", "null", "[]", "{}", R"({"torus":{}})", R"({"sphere":{}})",
                          R"({"sphere":{"radius":"53"}})", R"({"sphere":{"radius":1}})",
                          R"({"plane":{},"sphere":{"radius":53}})", "not json"}) {
    EXPECT_FALSE(SurfaceFromJson(bad).ok()) << bad;
  }
  EXPECT_EQ(SurfaceFromJson(R"({"sphere":{"radius":1}})").status().message(),
            "sphere radius must be within 2..1000");
}

}  // namespace
}  // namespace games_hub
