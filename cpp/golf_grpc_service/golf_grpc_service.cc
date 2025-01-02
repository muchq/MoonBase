#include "cpp/golf_grpc_service/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include "protos/golf_grpc/golf.pb.h"

using golf_grpc::DiscardDrawRequest;
using golf_grpc::DiscardDrawResponse;
using golf_grpc::KnockRequest;
using golf_grpc::KnockResponse;
using golf_grpc::NewGameRequest;
using golf_grpc::NewGameResponse;
using golf_grpc::PeekRequest;
using golf_grpc::PeekResponse;
using golf_grpc::RegisterUserRequest;
using golf_grpc::RegisterUserResponse;
using golf_grpc::SwapForDiscardRequest;
using golf_grpc::SwapForDiscardResponse;
using golf_grpc::SwapForDrawRequest;
using golf_grpc::SwapForDrawResponse;
using grpc::ServerContext;
using grpc::Status;

Status GolfServiceImpl::RegisterUser(ServerContext* context, const RegisterUserRequest* request,
                                     RegisterUserResponse* response) {
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
