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
/// of one, centred on the origin, with players on its wall; the
/// glasshouse is the plane's floor inside four glass walls standing at
/// its edges. A room chooses at creation and changes it with the lobby's
/// setGeometry; the plaza starts a plane.
struct Surface {
  enum class Kind { kPlane, kSphere, kGlasshouse };
  Kind kind = Kind::kPlane;
  /// The sphere's radius; unused by the other two.
  double radius = 0.0;

  static Surface Plane() { return {}; }
  static Surface Sphere(double radius) { return {Kind::kSphere, radius}; }
  static Surface Glasshouse() { return {Kind::kGlasshouse}; }

  /// Whether the world has glass to splat deja's tape onto (#1150): the
  /// gate on every byte of HTTP the hub sends deja's way.
  [[nodiscard]] bool has_glass() const { return kind == Kind::kGlasshouse; }
  /// Whether players stand on the ground plane's floor. The glasshouse's
  /// floor is the plane's to the letter — same extent, same y — because
  /// the glass is a boundary at the edges it already had, not a new
  /// place to stand.
  [[nodiscard]] bool is_flat() const { return kind == Kind::kPlane || kind == Kind::kGlasshouse; }

  /// The flat surfaces' half extent: x and z within ±50, and where a
  /// glasshouse's four walls stand.
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
  /// surface. A sphere snaps it to the wall; a flat surface takes it as
  /// is, and refuses anything off the floor — the glass included.
  std::optional<std::string> Settle(std::vector<double>& position) const;

  /// The nearest point of the surface to `position`, a settled point of
  /// any surface: where a player stands after their world changes
  /// shape. A flat surface clamps x and z and drops y; the sphere scales
  /// onto the wall, and puts a position with no direction (the origin)
  /// at [0, 0, -radius].
  std::vector<double> Place(const std::vector<double>& position) const;

  bool operator==(const Surface&) const = default;
};

/// The stored form, in the wire's spelling: {"plane":{}},
/// {"sphere":{"radius":R}} or {"glasshouse":{}}.
std::string SurfaceJson(const Surface& surface);
absl::StatusOr<Surface> SurfaceFromJson(std::string_view text);

}  // namespace games_hub

#endif
