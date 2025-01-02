#ifndef CPP_GOLF_GRPC_GOLF_GRPC_SERVICE_H
#define CPP_GOLF_GRPC_GOLF_GRPC_SERVICE_H

#include "protos/golf_grpc/golf.grpc.pb.h"

class GolfServiceImpl final : public golf_grpc::Golf::Service {
  grpc::Status RegisterUser(grpc::ServerContext* context,
                            const golf_grpc::RegisterUserRequest* request,
                            golf_grpc::RegisterUserResponse* response) override;
  grpc::Status NewGame(grpc::ServerContext* context, const golf_grpc::NewGameRequest* request,
                       golf_grpc::NewGameResponse* response) override;
  grpc::Status Peek(grpc::ServerContext* context, const golf_grpc::PeekRequest* request,
                    golf_grpc::PeekResponse* response) override;
  grpc::Status DiscardDraw(grpc::ServerContext* context,
                           const golf_grpc::DiscardDrawRequest* request,
                           golf_grpc::DiscardDrawResponse* response) override;
  grpc::Status SwapForDraw(grpc::ServerContext* context,
                           const golf_grpc::SwapForDrawRequest* request,
                           golf_grpc::SwapForDrawResponse* response) override;
  grpc::Status SwapForDiscard(grpc::ServerContext* context,
                              const golf_grpc::SwapForDiscardRequest* request,
                              golf_grpc::SwapForDiscardResponse* response) override;
  grpc::Status Knock(grpc::ServerContext* context, const golf_grpc::KnockRequest* request,
                     golf_grpc::KnockResponse* response) override;
};

#endif
