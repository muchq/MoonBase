#include "cpp/example_service/example_service.h"
#include "protos/example_service/helloworld.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <string>

using example_service::HelloReply;
using example_service::HelloRequest;
using grpc::ServerContext;
using grpc::Status;

Status GreeterServiceImpl::SayHello(ServerContext* context, const HelloRequest* request,
                                    HelloReply* reply) {
  std::string prefix("Hello ");
  reply->set_message(prefix + request->name());
  return Status::OK;
};
