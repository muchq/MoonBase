#ifndef CPP_PORTRAIT_TYPES_H
#define CPP_PORTRAIT_TYPES_H

#include <nlohmann/json.hpp>
#include <string>
#include <tuple>

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

struct TraceResponse {
  std::string base64_png;
  int width;
  int height;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TraceResponse, base64_png, width, height)

absl::Status validateTraceRequest(TraceRequest &request);

}  // namespace portrait

#endif