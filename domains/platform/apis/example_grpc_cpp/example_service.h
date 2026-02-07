#ifndef CPP_EXAMPLE_SERVICE_EXAMPLE_SERVICE_H
#define CPP_EXAMPLE_SERVICE_EXAMPLE_SERVICE_H

#include "domains/platform/protos/example_grpc/helloworld.grpc.pb.h"

class GreeterServiceImpl final : public example_service::Greeter::Service {
  grpc::Status SayHello(grpc::ServerContext* context, const example_service::HelloRequest* request,
                        example_service::HelloReply* reply) override;
};

#endif
