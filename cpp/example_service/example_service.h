#ifndef CPP_EXAMPLE_SERVICE_EXAMPLE_SERVICE_H
#define CPP_EXAMPLE_SERVICE_EXAMPLE_SERVICE_H

#include <grpcpp/grpcpp.h>

#include "protos/example_service/helloworld.grpc.pb.h"

class GreeterServiceImpl final : public example_service::Greeter::Service {
  grpc::Status SayHello(grpc::ServerContext* context, const example_service::HelloRequest* request,
                        example_service::HelloReply* reply) override;
};

#endif
