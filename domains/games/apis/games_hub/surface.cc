#include "domains/games/apis/games_hub/surface.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace games_hub {

using nlohmann::json;

namespace {
constexpr char kStoredForm[] =
    "geometry must be {\"plane\":{}}, {\"sphere\":{\"radius\":R}} or {\"glasshouse\":{}}";
}  // namespace

std::optional<std::string> Surface::RadiusProblem(double radius) {
  // NaN fails the comparison, so it is refused with the rest.
  if (!(radius >= kMinRadius) || !(radius <= kMaxRadius)) {
    return absl::StrCat("sphere radius must be within ", kMinRadius, "..", kMaxRadius);
  }
  return std::nullopt;
}

std::optional<std::string> Surface::Settle(std::vector<double>& position) const {
  if (position.size() != 3) return "position must be [x, y, z]";
  if (is_flat()) {
    // NaN fails every comparison, so it is refused by the bounds checks
    // themselves rather than by a separate finiteness rule.
    if (!(position[1] == 0.0)) return "y must be 0";
    if (!(std::abs(position[0]) <= kHalfExtent) || !(std::abs(position[2]) <= kHalfExtent)) {
      return "position out of bounds (±50)";
    }
    return std::nullopt;
  }
  // NaN and infinity fail the band check on their own.
  const std::string off_the_wall =
      absl::StrCat("position must be on the sphere (radius ", radius, ")");
  const double length = std::hypot(position[0], position[1], position[2]);
  if (!(std::abs(length - radius) <= kSnap)) return off_the_wall;
  const double onto = radius / length;
  for (double& component : position) component *= onto;
  return std::nullopt;
}

std::vector<double> Surface::Place(const std::vector<double>& position) const {
  if (is_flat()) {
    return {std::clamp(position[0], -kHalfExtent, kHalfExtent), 0.0,
            std::clamp(position[2], -kHalfExtent, kHalfExtent)};
  }
  const double length = std::hypot(position[0], position[1], position[2]);
  if (!(length > 0.0)) return {0.0, 0.0, -radius};
  const double onto = radius / length;
  return {position[0] * onto, position[1] * onto, position[2] * onto};
}

std::string SurfaceJson(const Surface& surface) {
  json out;
  const std::string kind(SurfaceKindName(surface));
  if (surface.kind == Surface::Kind::kSphere) {
    out[kind] = {{"radius", surface.radius}};
  } else {
    out[kind] = json::object();
  }
  return out.dump();
}

absl::StatusOr<Surface> SurfaceFromJson(std::string_view text) {
  const json parsed = json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (!parsed.is_object() || parsed.size() != 1) {
    return absl::InvalidArgumentError(kStoredForm);
  }
  if (parsed.contains("plane")) return Surface::Plane();
  if (parsed.contains("glasshouse")) return Surface::Glasshouse();
  const auto sphere = parsed.find("sphere");
  if (sphere == parsed.end() || !sphere->is_object() || !sphere->contains("radius") ||
      !(*sphere)["radius"].is_number()) {
    return absl::InvalidArgumentError(kStoredForm);
  }
  const double radius = (*sphere)["radius"].get<double>();
  if (const auto problem = Surface::RadiusProblem(radius)) {
    return absl::InvalidArgumentError(*problem);
  }
  return Surface::Sphere(radius);
}

}  // namespace games_hub
