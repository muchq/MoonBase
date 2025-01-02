#include "cpp/golf_grpc_service/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include <string>

#include "protos/golf_grpc/golf.grpc.pb.h"

using golf_grpc::HelloReply;
using golf_grpc::HelloRequest;
using grpc::ServerContext;
using grpc::Status;

Status GreeterServiceImpl::SayHello(ServerContext* context, const HelloRequest* request,
                                    HelloReply* reply) {
  std::string prefix("Hello ");
  reply->set_message(prefix + request->name());
  return Status::OK;
};
