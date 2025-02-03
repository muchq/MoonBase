#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include "cpp/cards/golf/in_memory_game_store.h"
#include "cpp/golf_grpc/client/golf_grpc_client.h"
#include "cpp/golf_grpc/server/test_helpers.h"
#include "protos/golf_grpc/golf.grpc.pb.h"

using golf_grpc::Golf;
using namespace test_helpers;

TEST(SERVICE_TEST, RegisterUser) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_id{"hello@example.org"};

  // Act
  auto status1 = client.RegisterUser(user_id);
  auto status2 = client.RegisterUser(user_id);

  // Assert
  EXPECT_TRUE(status1.ok());
  EXPECT_EQ(status2.code(), absl::StatusCode::kAlreadyExists);

  server->Shutdown();
}

TEST(SERVICE_TEST, NewGame) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_id{"hello@example.org"};

  // Act
  auto status1 = client.RegisterUser(user_id);
  auto status_or_game = client.NewGame(user_id, 2);

  // Assert
  EXPECT_TRUE(status1.ok());
  EXPECT_TRUE(status_or_game.ok());

  const auto& game_state = status_or_game.value();

  EXPECT_EQ(game_state.number_of_players(), 2);

  server->Shutdown();
}

TEST(SERVICE_TEST, NewGameFailsWithoutUser) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_id{"hello@example.org"};

  // Act
  auto status = client.NewGame(user_id, 2);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().message(), "unknown user");

  server->Shutdown();
}

TEST(SERVICE_TEST, NewGameFailsOnNegativeUserCount) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_id{"hello@example.org"};

  // Act
  auto register_status = client.RegisterUser(user_id);
  auto new_game_status_or_game = client.NewGame(user_id, -3);

  // Assert
  EXPECT_TRUE(register_status.ok());
  EXPECT_FALSE(new_game_status_or_game.ok());
  EXPECT_EQ(new_game_status_or_game.status().message(), "2 to 5 players");

  server->Shutdown();
}

TEST(SERVICE_TEST, Peek) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_id{"hello@example.org"};
  const std::string user_two{"bonk@example.org"};

  // Act
  auto register_status = client.RegisterUser(user_id);
  auto register_user_two_status = client.RegisterUser(user_two);
  auto new_game_status_or_game = client.NewGame(user_id, 2);
  auto join_game_status_or_game = client.JoinGame(user_two, new_game_status_or_game->game_id());
  auto peek_status_or_game = client.PeekAtDrawPile(user_id, new_game_status_or_game->game_id());

  // Assert
  EXPECT_TRUE(new_game_status_or_game.ok());
  EXPECT_TRUE(register_user_two_status.ok());
  EXPECT_TRUE(join_game_status_or_game.ok());
  EXPECT_TRUE(peek_status_or_game.ok());
  
  server->Shutdown();
}
