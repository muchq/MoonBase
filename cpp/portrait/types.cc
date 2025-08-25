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

absl::Status validateTraceRequest(TraceRequest &traceRequest) {
  auto perspectiveStatus = validatePerspective(traceRequest.perspective);
  if (!perspectiveStatus.ok()) {
    return perspectiveStatus;
  }
  return absl::OkStatus();
}
}  // namespace portrait