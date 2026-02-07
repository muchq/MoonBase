#ifndef CPP_GOLF_GRPC_SERVER_TEST_HELPERS_H
#define CPP_GOLF_GRPC_SERVER_TEST_HELPERS_H

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>

#include "domains/games/libs/cards/golf/in_memory_game_store.h"
#include "domains/games/protos/golf_grpc/golf.grpc.pb.h"

namespace test_helpers {

using golf_grpc::Golf;
using golf_grpc::NewGameRequest;
using golf_grpc::NewGameResponse;
using golf_grpc::RegisterUserRequest;
using golf_grpc::RegisterUserResponse;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::Status;

inline auto MakeAllocatedGolfService() -> std::unique_ptr<GolfServiceImpl> {
  auto store = std::make_shared<golf::InMemoryGameStore>();
  auto dealer = std::make_shared<golf::NoShuffleDealer>();
  auto manager = std::make_shared<golf::GameManager>(store, dealer);
  return std::make_unique<GolfServiceImpl>(manager);
}

inline auto MakeAllocatedServer(GolfServiceImpl* service) -> std::unique_ptr<Server> {
  ServerBuilder builder;
  builder.RegisterService(service);
  std::unique_ptr server(builder.BuildAndStart());
  return server;
}
}  // namespace test_helpers

#endif