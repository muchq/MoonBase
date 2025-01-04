#include "golf_grpc_client.h"

namespace golf_grpc {

using grpc::ClientContext;
using grpc::Status;
using std::string;

StatusOr<string> GolfClient::RegisterUser(const string& username) {
  RegisterUserRequest request;
  request.set_user_id(username);

  RegisterUserResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->RegisterUser(&context, request, &rpc_reply);
  if (rpc_status.ok()) {
    return username;
  }

  return GrpcStatusToAbseil(rpc_status);
}

absl::Status GolfClient::GrpcStatusToAbseil(Status status) {
  auto status_code = absl::StatusCode(status.error_code());
  return absl::Status(status_code, status.error_message());
}

}  // namespace golf_grpc
