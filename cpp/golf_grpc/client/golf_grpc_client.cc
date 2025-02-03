#include "golf_grpc_client.h"

#include <grpcpp/client_context.h>

#include "cpp/futility/status/status.h"

namespace golf_grpc {

using grpc::ClientContext;
using std::string;

using futility::status::GrpcToAbseil;

absl::Status GolfClient::RegisterUser(const string& user_id) const {
  RegisterUserRequest request;
  request.set_user_id(user_id);

  RegisterUserResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->RegisterUser(&context, request, &rpc_reply);
  return GrpcToAbseil(rpc_status);
}

absl::StatusOr<GameState> GolfClient::NewGame(const std::string& user_id,
                                              int number_of_players) const {
  NewGameRequest request;
  request.set_user_id(user_id);
  request.set_number_of_players(number_of_players);

  NewGameResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->NewGame(&context, request, &rpc_reply);
  if (!rpc_status.ok()) {
    return GrpcToAbseil(rpc_status);
  }
  return rpc_reply.game_state();
}

absl::StatusOr<GameState> GolfClient::JoinGame(const std::string& user_id,
                                               const std::string& game_id) const {
  JoinGameRequest request;
  request.set_user_id(user_id);
  request.set_game_id(game_id);

  JoinGameResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->JoinGame(&context, request, &rpc_reply);
  if (!rpc_status.ok()) {
    return GrpcToAbseil(rpc_status);
  }
  return rpc_reply.game_state();
}

absl::StatusOr<GameState> GolfClient::PeekAtDrawPile(const std::string& user_id,
                                                     const std::string& game_id) const {
  PeekRequest request;
  request.set_user_id(user_id);
  request.set_game_id(game_id);

  PeekResponse rpc_reply;
  ClientContext context;

  auto rpc_status = stub_->Peek(&context, request, &rpc_reply);
  if (!rpc_status.ok()) {
    return GrpcToAbseil(rpc_status);
  }
  return rpc_reply.game_state();
}
}  // namespace golf_grpc
