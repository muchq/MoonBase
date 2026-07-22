#include "domains/graphics/apis/portrait/smithy_handler.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/graphics/apis/portrait/types.h"
#include "smithy/core/blob.h"
#include "smithy/core/error.h"

namespace portrait {
namespace {

namespace gen = moonbase::portrait;

Vec3 toVec3(const std::vector<double>& v) {
  // @length(3, 3) validation guarantees exactly three elements.
  return Vec3{v[0], v[1], v[2]};
}

Color toColor(const std::vector<int32_t>& c) {
  // @range(0, 255) validation guarantees the channels fit unsigned char.
  return Color{static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]),
               static_cast<unsigned char>(c[2])};
}

LightType toLightType(const gen::LightType& lightType) {
  switch (lightType.value()) {
    case gen::LightType::Value::kAmbient:
      return AMBIENT;
    case gen::LightType::Value::kPoint:
      return POINT;
    case gen::LightType::Value::kDirectional:
      return DIRECTIONAL;
    default:
      return UNKNOWN;  // rejected by validateTraceRequest -> InvalidSceneError
  }
}

TraceRequest toDomainRequest(const gen::TraceInput& input) {
  TraceRequest request;
  // Absent backgroundColor keeps Scene's own {0, 0, 0} default member value.
  if (input.scene.backgroundColor.has_value()) {
    request.scene.backgroundColor = toColor(*input.scene.backgroundColor);
  }
  request.scene.backgroundStarProbability = input.scene.backgroundStarProbability;
  request.scene.spheres.reserve(input.scene.spheres.size());
  for (const auto& sphere : input.scene.spheres) {
    request.scene.spheres.push_back(Sphere{.center = toVec3(sphere.center),
                                           .radius = sphere.radius,
                                           .color = toColor(sphere.color),
                                           .specular = sphere.specular,
                                           .reflective = sphere.reflective});
  }
  request.scene.lights.reserve(input.scene.lights.size());
  for (const auto& light : input.scene.lights) {
    request.scene.lights.push_back(Light{.lightType = toLightType(light.lightType),
                                         .intensity = light.intensity,
                                         .position = toVec3(light.position)});
  }
  request.perspective.cameraPosition = toVec3(input.perspective.cameraPosition);
  request.perspective.cameraFocus = toVec3(input.perspective.cameraFocus);
  request.output.width = input.output.width;
  request.output.height = input.output.height;
  return request;
}

}  // namespace

smithy::Outcome<gen::TraceOutput> SmithyTracerHandler::Trace(
    const gen::TraceInput& input, const smithy::server::RequestContext& /*context*/) {
  TraceRequest request = toDomainRequest(input);
  absl::StatusOr<TraceResponse> response = tracer_service_.trace(request);
  if (!response.ok()) {
    const std::string message(response.status().message());
    if (response.status().code() == absl::StatusCode::kInvalidArgument) {
      smithy::Error error = smithy::Error::Modeled("InvalidSceneError", message);
      error.set_detail(gen::InvalidSceneError{.message = message});
      return error;
    }
    return smithy::Error::Unknown(message);  // undeclared -> non-leaking 500
  }

  gen::TraceOutput output;
  output.base64_png = smithy::Blob(std::move(response->png_bytes));
  output.width = response->width;
  output.height = response->height;
  return output;
}

}  // namespace portrait
