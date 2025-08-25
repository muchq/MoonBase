#ifndef CPP_PORTRAIT_TYPES_H
#define CPP_PORTRAIT_TYPES_H

#include <nlohmann/json.hpp>

#include "absl/status/status.h"
#include "cpp/tracy/tracy.h"

namespace portrait {
using namespace nlohmann::literals;

struct Vec3 {
  double x, y, z;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3, x, y, z)

struct Color {
  unsigned char r, g, b;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Color, r, g, b)

struct Sphere {
  Vec3 center;
  double radius;
  Color color;
  double specular;
  double reflective;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Sphere, center, radius, color, specular, reflective)

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
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Light, lightType, intensity, position)

struct Perspective {
  Vec3 cameraPosition;
  Vec3 cameraFocus;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Perspective, cameraPosition, cameraFocus)

struct Scene {
  Color backgroundColor = {0, 0, 0};
  double backgroundStarProbability = 0.0;
  std::vector<Sphere> spheres;
  std::vector<Light> lights;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene, backgroundColor, backgroundStarProbability,
                                                spheres, lights)

struct Output {
  int width;
  int height;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Output, width, height)

struct TraceRequest {
  Scene scene;
  Perspective perspective;
  Output output;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TraceRequest, scene, perspective, output)

absl::Status validateTraceRequest(TraceRequest &request);

}  // namespace portrait

#endif