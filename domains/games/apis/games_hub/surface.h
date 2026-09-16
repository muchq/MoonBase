#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_SURFACE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_SURFACE_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace games_hub {

/// The shape of a room's world (#1554): where its players stand. The
/// plane is the ground the lobby always had; the sphere is the inside
/// of one, centred on the origin, with players on its wall. A room
/// chooses at creation and keeps the choice for its life; the plaza is
/// a plane.
struct Surface {
  enum class Kind { kPlane, kSphere };
  Kind kind = Kind::kPlane;
  /// The sphere's radius; unused on the plane.
  double radius = 0.0;

  static Surface Plane() { return {}; }
  static Surface Sphere(double radius) { return {Kind::kSphere, radius}; }

  /// The plane's half extent: x and z within ±50.
  static constexpr double kHalfExtent = 50.0;
  /// A sphere's radius: at least room for an avatar to stand in, at
  /// most one where doubles still resolve kSnap and the UI can draw it.
  static constexpr double kMinRadius = 2.0;
  static constexpr double kMaxRadius = 1000.0;
  /// How far off a sphere's wall a position may arrive and still be put
  /// on it: an avatar's radius. A client steps along the tangent, so a
  /// step lands just outside the wall; anything farther is malformed.
  static constexpr double kSnap = 1.0;
  static_assert(kMinRadius > kSnap,
                "an admitted position is never at the origin, so the snap never divides by zero");

  /// The reason a sphere of this radius is refused, else nullopt.
  static std::optional<std::string> RadiusProblem(double radius);

  /// Settles a position onto the surface: the reason it is refused (the
  /// reason a client is told), else nullopt with `position` on the
  /// surface. A sphere snaps it to the wall; the plane takes it as is.
  std::optional<std::string> Settle(std::vector<double>& position) const;

  bool operator==(const Surface&) const = default;
};

/// The stored form, in the wire's spelling: {"plane":{}} or
/// {"sphere":{"radius":R}}.
std::string SurfaceJson(const Surface& surface);
absl::StatusOr<Surface> SurfaceFromJson(std::string_view text);

}  // namespace games_hub

#endif
