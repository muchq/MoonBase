// A room's surface: what it refuses, what it settles, and how it is
// stored. The plane keeps the lobby's original rules to the letter; the
// sphere puts a near-enough position on its wall and refuses the rest.

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
  for (const Surface& surface : {Surface::Plane(), Surface::Sphere(53), Surface::Sphere(2.5)}) {
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
