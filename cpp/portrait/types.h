#ifndef CPP_PORTRAIT_TYPES_H
#define CPP_PORTRAIT_TYPES_H

#include <nlohmann/json.hpp>
#include <string>
#include <tuple>

#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "cpp/tracy/tracy.h"

namespace portrait {
using namespace nlohmann::literals;

using Vec3 = std::tuple<double, double, double>;
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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Sphere, center, radius, color, specular, reflective)

template <typename H>
H AbslHashValue(H h, const Sphere& s) {
  return H::combine(std::move(h), s.center, s.radius, s.color, s.specular, s.reflective);
}

enum LightType { AMBIENT, POINT, DIRECTIONAL, UNKNOWN };
NLOHMANN_JSON_SERIALIZE_ENUM(LightType, {
                                            {UNKNOWN, nullptr},
                                            {AMBIENT, "ambient"},
                                            {POINT, "point"},
                                            {DIRECTIONAL, "directional"},
                                        })

struct Light {
  LightType lightType;
  double intensity;
  Vec3 position;

  bool operator==(const Light& other) const {
    return lightType == other.lightType && intensity == other.intensity &&
           position == other.position;
  }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Light, lightType, intensity, position)

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Perspective, cameraPosition, cameraFocus)

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene, backgroundColor, backgroundStarProbability,
                                                spheres, lights)

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Output, width, height)

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TraceRequest, scene, perspective, output)

template <typename H>
H AbslHashValue(H h, const TraceRequest& tr) {
  return H::combine(std::move(h), tr.scene, tr.perspective, tr.output);
}

struct TraceResponse {
  std::string base64_png;
  int width;
  int height;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TraceResponse, base64_png, width, height)

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