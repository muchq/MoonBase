#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include "cpp/cards/golf/in_memory_game_store.h"
#include "protos/golf_grpc/golf.grpc.pb.h"

using golf_grpc::Golf;
using golf_grpc::RegisterUserRequest;
using golf_grpc::RegisterUserResponse;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::Status;

namespace {
auto MakeAllocatedGolfService() -> std::unique_ptr<GolfServiceImpl> {
  auto store = std::make_shared<golf::InMemoryGameStore>();
  golf::GameManager const game_manager{store};
  return std::make_unique<GolfServiceImpl>(game_manager);
}

auto MakeAllocatedServer(GolfServiceImpl* service) -> std::unique_ptr<Server> {
  ServerBuilder builder;
  builder.RegisterService(service);
  std::unique_ptr server(builder.BuildAndStart());
  return server;
}

auto CallRegisterUser(std::string user_id, Golf::Stub* stub) -> Status {
  ClientContext context;
  context.AddMetadata("app-name", "test-app");

  RegisterUserRequest req;
  req.set_user_id(user_id);
  RegisterUserResponse res;

  return stub->RegisterUser(&context, req, &res);
}
}  // namespace

TEST(SERVICE_TEST, RegisterUser) {
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = Golf::NewStub(server->InProcessChannel({}));

  const std::string user_id{"hello@example.org"};

  auto status1 = CallRegisterUser(user_id, stub.get());

  EXPECT_TRUE(status1.ok());

  auto status2 = CallRegisterUser(user_id, stub.get());

  EXPECT_EQ(status2.error_code(), grpc::StatusCode::ALREADY_EXISTS);

  server->Shutdown();
}
