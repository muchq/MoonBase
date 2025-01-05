#include "status.h"

namespace futility {
namespace status {

absl::Status GrpcToAbseil(grpc::Status status) {
  if (status.ok()) {
    return absl::OkStatus();
  }
  auto status_code = absl::StatusCode(status.error_code());
  return absl::Status(status_code, status.error_message());
}

grpc::Status AbseilToGrpc(absl::Status status) {
  if (status.ok()) {
    return grpc::Status::OK;
  }
  return grpc::Status(static_cast<grpc::StatusCode>(status.code()), status.message().data());
}

}  // namespace status
}  // namespace futility
