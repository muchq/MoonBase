#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include "protos/golf_grpc/golf.pb.h"

using namespace golf_grpc;
using grpc::ServerContext;
using grpc::Status;

Status GolfServiceImpl::RegisterUser(ServerContext* context, const RegisterUserRequest* request,
                                     RegisterUserResponse* response) {
  auto res = gm.registerUser(request->user_id());
  if (!res.ok()) {
    auto status = res.status();
    return Status(static_cast<grpc::StatusCode>(status.code()), status.message().data());
  }

  return Status::OK;
};

Status GolfServiceImpl::NewGame(ServerContext* context, const NewGameRequest* request,
                                NewGameResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::Peek(ServerContext* context, const PeekRequest* request,
                             PeekResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::DiscardDraw(ServerContext* context, const DiscardDrawRequest* request,
                                    DiscardDrawResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::SwapForDraw(ServerContext* context, const SwapForDrawRequest* request,
                                    SwapForDrawResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::SwapForDiscard(ServerContext* context, const SwapForDiscardRequest* request,
                                       SwapForDiscardResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::Knock(ServerContext* context, const KnockRequest* request,
                              KnockResponse* response) {
  return Status::OK;
};
