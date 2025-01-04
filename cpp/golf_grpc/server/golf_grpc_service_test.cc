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

TEST(SERVICE_TEST, BasicAssertions) {
  auto store = std::make_shared<golf::InMemoryGameStore>();
  golf::GameManager const game_manager{store};
  GolfServiceImpl service{game_manager};

  ServerBuilder builder;
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  auto channel = server->InProcessChannel({});
  auto stub = Golf::NewStub(server->InProcessChannel({}));

  ClientContext context;
  context.AddMetadata("app-name", "test-app");

  RegisterUserRequest req;
  req.set_user_id("hello@example.org");
  RegisterUserResponse res;

  auto status = stub->RegisterUser(&context, req, &res);

  EXPECT_TRUE(status.ok());

  server->Shutdown();
}
