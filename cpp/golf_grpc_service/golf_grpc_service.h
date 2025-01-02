#ifndef CPP_GOLF_GRPC_GOLF_GRPC_SERVICE_H
#define CPP_GOLF_GRPC_GOLF_GRPC_SERVICE_H

#include "protos/golf_grpc/golf.grpc.pb.h"

class GolfServiceImpl final : public golf_grpc::Golf::Service {
  grpc::Status SayHello(grpc::ServerContext* context, const golf_grpc::HelloRequest* request,
                        golf_grpc::HelloReply* reply) override;
};

#endif
