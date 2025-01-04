#ifndef CPP_GOLF_GRPC_CLIENT_GOLF_GRPC_CLIENT_H
#define CPP_GOLF_GRPC_CLIENT_GOLF_GRPC_CLIENT_H

#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "protos/golf_grpc/golf.grpc.pb.h"

namespace golf_grpc {

using absl::StatusOr;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::unordered_map;

class GolfClient {
 public:
  explicit GolfClient(shared_ptr<Golf::StubInterface> stub) : stub_(std::move(stub)) {}

  StatusOr<string> RegisterUser(const string& username);

 private:
  absl::Status GrpcStatusToAbseil(grpc::Status status);
  shared_ptr<Golf::StubInterface> stub_;
};

}  // namespace golf_grpc

#endif  // CPP_GOLF_GRPC_CLIENT_GOLF_GRPC_CLIENT_H
