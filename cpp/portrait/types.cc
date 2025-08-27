#include "cpp/portrait/types.h"

#include "absl/status/status.h"

namespace portrait {

absl::Status validateVec3(Vec3 &vec3) { return absl::OkStatus(); }

absl::Status validatePerspective(Perspective &perspective) {
  auto positionStatus = validateVec3(perspective.cameraPosition);
  if (!positionStatus.ok()) {
    return positionStatus;
  }
  return validateVec3(perspective.cameraFocus);
}

absl::Status validateScene(const Scene &scene) {
  if (scene.spheres.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "empty scene");
  }
  if (scene.spheres.size() > 10) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "max spheres is 10");
  }
  return absl::OkStatus();
}

absl::Status validateOutput(const Output &output) {
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
  return absl::OkStatus();
}

absl::Status validateTraceRequest(TraceRequest &traceRequest) {
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