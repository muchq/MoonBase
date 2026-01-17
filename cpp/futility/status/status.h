#ifndef CPP_FUTILITY_STATUS_STATUS_H
#define CPP_FUTILITY_STATUS_STATUS_H

/// @file status.h
/// @brief Conversion utilities between gRPC and Abseil status types.
///
/// When building services that use both gRPC (for transport) and Abseil
/// (for internal error handling), these utilities provide seamless conversion
/// between the two status types.
///
/// Example usage:
/// @code
///   // In a gRPC service implementation
///   grpc::Status MyService::DoSomething(grpc::ServerContext* ctx, ...) {
///     absl::Status result = internal_logic();
///     return futility::status::AbseilToGrpc(result);
///   }
///
///   // When calling a gRPC service
///   grpc::Status grpc_status = stub->CallMethod(&ctx, request, &response);
///   absl::Status status = futility::status::GrpcToAbseil(grpc_status);
///   if (!status.ok()) {
///     LOG(ERROR) << "Call failed: " << status.message();
///   }
/// @endcode

#include "absl/status/status.h"
#include "grpcpp/support/status.h"

namespace futility {
namespace status {

/// @brief Converts a gRPC status to an Abseil status.
/// @param status The gRPC status to convert.
/// @return An equivalent absl::Status with the same code and message.
absl::Status GrpcToAbseil(grpc::Status status);

/// @brief Converts an Abseil status to a gRPC status.
/// @param status The Abseil status to convert.
/// @return An equivalent grpc::Status with the same code and message.
grpc::Status AbseilToGrpc(absl::Status status);

}  // namespace status
}  // namespace futility

#endif
