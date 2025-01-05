#include "golf_grpc_client.h"

#include "cpp/futility/status/status.h"

namespace golf_grpc {

using grpc::ClientContext;
using std::string;

using futility::status::GrpcToAbseil;

absl::Status GolfClient::RegisterUser(const string& user_id) {
  RegisterUserRequest request;
  request.set_user_id(user_id);

  RegisterUserResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->RegisterUser(&context, request, &rpc_reply);
  return GrpcToAbseil(rpc_status);
}

}  // namespace golf_grpc
