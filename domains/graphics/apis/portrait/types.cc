#include "domains/graphics/apis/portrait/types.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace portrait {
namespace {

/// Every rejection here is an InvalidArgument the handler turns into
/// InvalidSceneError, so the two things that vary are the message and the
/// member it belongs to. `field` may be empty for a rule that spans members
/// (camera position vs focus, aspect ratio) — those genuinely have no single
/// member to blame, which is why InvalidSceneError::field is optional.
absl::Status invalidScene(std::string_view field, std::string_view message) {
  absl::Status status(absl::StatusCode::kInvalidArgument, message);
  if (!field.empty()) {
    status.SetPayload(std::string(kInvalidFieldPayloadUrl), absl::Cord(field));
  }
  return status;
}

absl::Status validateColor(const Color& color) {
  // Note: Color uses unsigned char, which automatically constrains values to 0-255.
  // Values > 255 wrap around when assigned, so this validation can't detect them.
  // This would need to be handled at JSON parsing time if needed.
  return absl::OkStatus();
}

absl::Status validateSphere(const Sphere& sphere, std::string_view path) {
  if (!validateVec3(const_cast<Vec3&>(sphere.center)).ok()) {
    return invalidScene(absl::StrCat(path, "/center"), "Invalid sphere center");
  }

  if (std::isnan(sphere.radius)) {
    return invalidScene(absl::StrCat(path, "/radius"), "Sphere radius is NaN");
  }

  if (std::isinf(sphere.radius)) {
    return invalidScene(absl::StrCat(path, "/radius"), "Sphere radius is infinite");
  }

  if (sphere.radius <= 0.0) {
    return invalidScene(absl::StrCat(path, "/radius"), "Sphere radius must be positive");
  }

  if (sphere.radius > 10000.0) {
    return invalidScene(absl::StrCat(path, "/radius"), "Sphere radius exceeds maximum (10000)");
  }

  auto colorStatus = validateColor(sphere.color);
  if (!colorStatus.ok()) {
    return colorStatus;
  }

  if (std::isnan(sphere.specular)) {
    return invalidScene(absl::StrCat(path, "/specular"), "Sphere specular is NaN");
  }

  if (std::isinf(sphere.specular)) {
    return invalidScene(absl::StrCat(path, "/specular"), "Sphere specular is infinite");
  }

  if (sphere.specular < 0.0) {
    return invalidScene(absl::StrCat(path, "/specular"), "Sphere specular cannot be negative");
  }

  if (sphere.specular > 1000.0) {
    return invalidScene(absl::StrCat(path, "/specular"), "Sphere specular exceeds maximum (1000)");
  }

  if (std::isnan(sphere.reflective)) {
    return invalidScene(absl::StrCat(path, "/reflective"), "Sphere reflective is NaN");
  }

  if (std::isinf(sphere.reflective)) {
    return invalidScene(absl::StrCat(path, "/reflective"), "Sphere reflective is infinite");
  }

  if (sphere.reflective < 0.0) {
    return invalidScene(absl::StrCat(path, "/reflective"), "Sphere reflective cannot be negative");
  }

  if (sphere.reflective > 1.0) {
    return invalidScene(absl::StrCat(path, "/reflective"), "Sphere reflective cannot exceed 1.0");
  }

  return absl::OkStatus();
}

absl::Status validateLight(const Light& light, std::string_view path) {
  if (light.lightType == UNKNOWN) {
    return invalidScene(absl::StrCat(path, "/lightType"), "Light type cannot be UNKNOWN");
  }

  if (std::isnan(light.intensity)) {
    return invalidScene(absl::StrCat(path, "/intensity"), "Light intensity is NaN");
  }

  if (std::isinf(light.intensity)) {
    return invalidScene(absl::StrCat(path, "/intensity"), "Light intensity is infinite");
  }

  if (light.intensity < 0.0) {
    return invalidScene(absl::StrCat(path, "/intensity"), "Light intensity cannot be negative");
  }

  if (light.intensity > 10.0) {
    return invalidScene(absl::StrCat(path, "/intensity"), "Light intensity exceeds maximum (10)");
  }

  if (!validateVec3(const_cast<Vec3&>(light.position)).ok()) {
    return invalidScene(absl::StrCat(path, "/position"), "Invalid light position");
  }

  return absl::OkStatus();
}

}  // namespace

std::optional<std::string> invalidField(const absl::Status& status) {
  std::optional<absl::Cord> payload = status.GetPayload(std::string(kInvalidFieldPayloadUrl));
  if (!payload.has_value()) {
    return std::nullopt;
  }
  return std::string(*payload);
}

absl::Status validateVec3(Vec3& vec3, std::string_view field) {
  double x = std::get<0>(vec3);
  double y = std::get<1>(vec3);
  double z = std::get<2>(vec3);

  if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
    return invalidScene(field, "Vec3 contains NaN");
  }

  if (std::isinf(x) || std::isinf(y) || std::isinf(z)) {
    return invalidScene(field, "Vec3 contains infinity");
  }

  return absl::OkStatus();
}

absl::Status validatePerspective(Perspective& perspective) {
  auto positionStatus = validateVec3(perspective.cameraPosition, "/perspective/cameraPosition");
  if (!positionStatus.ok()) {
    return positionStatus;
  }

  auto focusStatus = validateVec3(perspective.cameraFocus, "/perspective/cameraFocus");
  if (!focusStatus.ok()) {
    return focusStatus;
  }

  if (perspective.cameraPosition == perspective.cameraFocus) {
    // Two members are jointly at fault, so neither one is the field to name.
    return invalidScene("", "Camera position and focus cannot be the same");
  }

  return absl::OkStatus();
}

absl::Status validateScene(const Scene& scene) {
  if (scene.spheres.empty()) {
    return invalidScene("/scene/spheres", "empty scene");
  }
  if (scene.spheres.size() > 10) {
    return invalidScene("/scene/spheres", "max spheres is 10");
  }

  for (std::size_t i = 0; i < scene.spheres.size(); ++i) {
    auto sphereStatus = validateSphere(scene.spheres[i], absl::StrCat("/scene/spheres/", i));
    if (!sphereStatus.ok()) {
      return sphereStatus;
    }
  }

  for (std::size_t i = 0; i < scene.lights.size(); ++i) {
    auto lightStatus = validateLight(scene.lights[i], absl::StrCat("/scene/lights/", i));
    if (!lightStatus.ok()) {
      return lightStatus;
    }
  }

  auto bgColorStatus = validateColor(scene.backgroundColor);
  if (!bgColorStatus.ok()) {
    return bgColorStatus;
  }

  if (std::isnan(scene.backgroundStarProbability)) {
    return invalidScene("/scene/backgroundStarProbability", "Background star probability is NaN");
  }

  if (std::isinf(scene.backgroundStarProbability)) {
    return invalidScene("/scene/backgroundStarProbability",
                        "Background star probability is infinite");
  }

  if (scene.backgroundStarProbability < 0.0) {
    return invalidScene("/scene/backgroundStarProbability",
                        "Background star probability cannot be negative");
  }

  if (scene.backgroundStarProbability > 1.0) {
    return invalidScene("/scene/backgroundStarProbability",
                        "Background star probability cannot exceed 1.0");
  }

  return absl::OkStatus();
}

absl::Status validateOutput(const Output& output) {
  if (output.width < 0) {
    return invalidScene("/output/width", "Width cannot be negative");
  }

  if (output.height < 0) {
    return invalidScene("/output/height", "Height cannot be negative");
  }

  if (output.width < 20) {
    return invalidScene("/output/width", "min width is 20 pixels");
  }
  if (output.height < 20) {
    return invalidScene("/output/height", "min height is 20 pixels");
  }
  if (output.width > 1200) {
    return invalidScene("/output/width", "max width is 1200 pixels");
  }
  if (output.height > 1200) {
    return invalidScene("/output/height", "max height is 1200 pixels");
  }

  double aspectRatio = static_cast<double>(output.width) / static_cast<double>(output.height);
  if (aspectRatio > 50.0 || aspectRatio < 1.0 / 50.0) {
    // width and height are jointly at fault; the ratio is the rule.
    return invalidScene("", "Aspect ratio too extreme");
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
