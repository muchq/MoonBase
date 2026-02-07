#include "domains/graphics/apis/portrait/types.h"

#include <cmath>
#include <limits>

#include "absl/status/status.h"

namespace portrait {

absl::Status validateVec3(Vec3& vec3) {
  double x = std::get<0>(vec3);
  double y = std::get<1>(vec3);
  double z = std::get<2>(vec3);

  if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Vec3 contains NaN");
  }

  if (std::isinf(x) || std::isinf(y) || std::isinf(z)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Vec3 contains infinity");
  }

  return absl::OkStatus();
}

absl::Status validateColor(const Color& color) {
  // Note: Color uses unsigned char, which automatically constrains values to 0-255.
  // Values > 255 wrap around when assigned, so this validation can't detect them.
  // This would need to be handled at JSON parsing time if needed.
  return absl::OkStatus();
}

absl::Status validateSphere(const Sphere& sphere) {
  auto centerStatus = validateVec3(const_cast<Vec3&>(sphere.center));
  if (!centerStatus.ok()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid sphere center");
  }

  if (std::isnan(sphere.radius)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere radius is NaN");
  }

  if (std::isinf(sphere.radius)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere radius is infinite");
  }

  if (sphere.radius <= 0.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere radius must be positive");
  }

  if (sphere.radius > 10000.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Sphere radius exceeds maximum (10000)");
  }

  auto colorStatus = validateColor(sphere.color);
  if (!colorStatus.ok()) {
    return colorStatus;
  }

  if (std::isnan(sphere.specular)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere specular is NaN");
  }

  if (std::isinf(sphere.specular)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere specular is infinite");
  }

  if (sphere.specular < 0.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere specular cannot be negative");
  }

  if (sphere.specular > 1000.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Sphere specular exceeds maximum (1000)");
  }

  if (std::isnan(sphere.reflective)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere reflective is NaN");
  }

  if (std::isinf(sphere.reflective)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere reflective is infinite");
  }

  if (sphere.reflective < 0.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere reflective cannot be negative");
  }

  if (sphere.reflective > 1.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Sphere reflective cannot exceed 1.0");
  }

  return absl::OkStatus();
}

absl::Status validateLight(const Light& light) {
  if (light.lightType == UNKNOWN) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Light type cannot be UNKNOWN");
  }

  if (std::isnan(light.intensity)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Light intensity is NaN");
  }

  if (std::isinf(light.intensity)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Light intensity is infinite");
  }

  if (light.intensity < 0.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Light intensity cannot be negative");
  }

  if (light.intensity > 10.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Light intensity exceeds maximum (10)");
  }

  auto positionStatus = validateVec3(const_cast<Vec3&>(light.position));
  if (!positionStatus.ok()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid light position");
  }

  return absl::OkStatus();
}

absl::Status validatePerspective(Perspective& perspective) {
  auto positionStatus = validateVec3(perspective.cameraPosition);
  if (!positionStatus.ok()) {
    return positionStatus;
  }

  auto focusStatus = validateVec3(perspective.cameraFocus);
  if (!focusStatus.ok()) {
    return focusStatus;
  }

  if (perspective.cameraPosition == perspective.cameraFocus) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Camera position and focus cannot be the same");
  }

  return absl::OkStatus();
}

absl::Status validateScene(const Scene& scene) {
  if (scene.spheres.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "empty scene");
  }
  if (scene.spheres.size() > 10) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "max spheres is 10");
  }

  for (const auto& sphere : scene.spheres) {
    auto sphereStatus = validateSphere(sphere);
    if (!sphereStatus.ok()) {
      return sphereStatus;
    }
  }

  for (const auto& light : scene.lights) {
    auto lightStatus = validateLight(light);
    if (!lightStatus.ok()) {
      return lightStatus;
    }
  }

  auto bgColorStatus = validateColor(scene.backgroundColor);
  if (!bgColorStatus.ok()) {
    return bgColorStatus;
  }

  if (std::isnan(scene.backgroundStarProbability)) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Background star probability is NaN");
  }

  if (std::isinf(scene.backgroundStarProbability)) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Background star probability is infinite");
  }

  if (scene.backgroundStarProbability < 0.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Background star probability cannot be negative");
  }

  if (scene.backgroundStarProbability > 1.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Background star probability cannot exceed 1.0");
  }

  return absl::OkStatus();
}

absl::Status validateOutput(const Output& output) {
  if (output.width < 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Width cannot be negative");
  }

  if (output.height < 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Height cannot be negative");
  }

  if (output.width < 20) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "min width is 20 pixels");
  }
  if (output.height < 20) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "min height is 20 pixels");
  }
  if (output.width > 1200) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "max width is 1200 pixels");
  }
  if (output.height > 1200) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "max height is 1200 pixels");
  }

  double aspectRatio = static_cast<double>(output.width) / static_cast<double>(output.height);
  if (aspectRatio > 50.0 || aspectRatio < 1.0 / 50.0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Aspect ratio too extreme");
  }

  return absl::OkStatus();
}

absl::Status validateTraceRequest(TraceRequest& traceRequest) {
  auto perspectiveStatus = validatePerspective(traceRequest.perspective);
  if (!perspectiveStatus.ok()) {
    return perspectiveStatus;
  }

  auto sceneStatus = validateScene(traceRequest.scene);
  if (!sceneStatus.ok()) {
    return sceneStatus;
  }

  auto outputStatus = validateOutput(traceRequest.output);
  if (!outputStatus.ok()) {
    return outputStatus;
  }
  return absl::OkStatus();
}
}  // namespace portrait
