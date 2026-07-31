#ifndef CPP_PORTRAIT_TYPES_H
#define CPP_PORTRAIT_TYPES_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/status/status.h"

// TracerService's domain layer: the plain structs the renderer and its LRU
// cache key on, plus the cross-field validation rules the Smithy constraint
// traits can't express. Wire parsing belongs to the generated server
// (moonbase/portrait); the handler converts generated inputs to these types.

namespace portrait {

using Vec3 = std::tuple<double, double, double>;

/// Channels are unsigned char, so 0..255 is enforced by the type and an
/// out-of-range value wraps rather than arriving detectably — there is
/// nothing for a validate* rule to check, which is why none exists. The
/// generated server rejects the out-of-range case at the wire (@range on
/// ColorChannel) before it can wrap.
using Color = std::tuple<unsigned char, unsigned char, unsigned char>;

struct Sphere {
  Vec3 center;
  double radius;
  Color color;
  double specular;
  double reflective;

  bool operator==(const Sphere& other) const {
    return center == other.center && radius == other.radius && color == other.color &&
           specular == other.specular && reflective == other.reflective;
  }
};

template <typename H>
H AbslHashValue(H h, const Sphere& s) {
  return H::combine(std::move(h), s.center, s.radius, s.color, s.specular, s.reflective);
}

enum LightType { AMBIENT, POINT, DIRECTIONAL, UNKNOWN };

struct Light {
  LightType lightType;
  double intensity;
  Vec3 position;

  bool operator==(const Light& other) const {
    return lightType == other.lightType && intensity == other.intensity &&
           position == other.position;
  }
};

template <typename H>
H AbslHashValue(H h, const Light& l) {
  return H::combine(std::move(h), l.lightType, l.intensity, l.position);
}

struct Perspective {
  Vec3 cameraPosition;
  Vec3 cameraFocus;

  bool operator==(const Perspective& other) const {
    return cameraPosition == other.cameraPosition && cameraFocus == other.cameraFocus;
  }
};

template <typename H>
H AbslHashValue(H h, const Perspective& p) {
  return H::combine(std::move(h), p.cameraPosition, p.cameraFocus);
}

struct Scene {
  Color backgroundColor = {0, 0, 0};
  double backgroundStarProbability = 0.0;
  std::vector<Sphere> spheres;
  std::vector<Light> lights;

  bool operator==(const Scene& other) const {
    return backgroundColor == other.backgroundColor &&
           backgroundStarProbability == other.backgroundStarProbability &&
           spheres == other.spheres && lights == other.lights;
  }
};

template <typename H>
H AbslHashValue(H h, const Scene& s) {
  return H::combine(std::move(h), s.backgroundColor, s.backgroundStarProbability, s.spheres,
                    s.lights);
}

struct Output {
  int width;
  int height;

  bool operator==(const Output& other) const {
    return width == other.width && height == other.height;
  }
};

template <typename H>
H AbslHashValue(H h, const Output& o) {
  return H::combine(std::move(h), o.width, o.height);
}

struct TraceRequest {
  Scene scene;
  Perspective perspective;
  Output output;

  bool operator==(const TraceRequest& other) const {
    return scene == other.scene && perspective == other.perspective && output == other.output;
  }
};

template <typename H>
H AbslHashValue(H h, const TraceRequest& tr) {
  return H::combine(std::move(h), tr.scene, tr.perspective, tr.output);
}

struct TraceResponse {
  std::vector<std::uint8_t> png_bytes;
  int width;
  int height;
};

/// Payload key carrying the offending member's JSON-pointer path on an
/// InvalidArgument status ("/scene/spheres/0/radius"). The path form matches
/// the one the generated server's ValidationException uses for the
/// trait-expressible constraints, so a client sees one convention whichever
/// layer rejected the scene. The handler lifts it onto
/// InvalidSceneError::field.
inline constexpr std::string_view kInvalidFieldPayloadUrl = "type.moonbase.portrait/invalid-field";

/// The offending member's path, when the rule that failed named one. Absent
/// for rules that span members and for any status that isn't a validation
/// failure.
std::optional<std::string> invalidField(const absl::Status& status);

/// `field` is the JSON-pointer path this Vec3 occupies in the request, for
/// the payload above; empty when the caller reports its own path instead.
absl::Status validateVec3(const Vec3& vec3, std::string_view field = {});
absl::Status validatePerspective(Perspective& perspective);
absl::Status validateScene(const Scene& scene);
absl::Status validateOutput(const Output& output);
absl::Status validateTraceRequest(TraceRequest& request);

}  // namespace portrait

// std::hash specialization for TraceRequest to enable use with std::unordered_map
namespace std {
template <>
struct hash<portrait::TraceRequest> {
  size_t operator()(const portrait::TraceRequest& tr) const {
    return absl::Hash<portrait::TraceRequest>{}(tr);
  }
};
}  // namespace std

#endif
