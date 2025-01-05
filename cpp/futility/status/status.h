#ifndef CPP_FUTILITY_STATUS_STATUS_H
#define CPP_FUTILITY_STATUS_STATUS_H

#include "absl/status/status.h"
#include "grpcpp/support/status.h"

namespace futility {
namespace status {

absl::Status GrpcToAbseil(grpc::Status status);
grpc::Status AbseilToGrpc(absl::Status status);

}  // namespace status
}  // namespace futility

#endif
