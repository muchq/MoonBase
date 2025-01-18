#include "golf_grpc_client.h"

#include <gtest/gtest.h>

#include "protos/golf_grpc/golf_mock.grpc.pb.h"

using namespace golf_grpc;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

TEST(GolfClient, RegisterUserRpcSuccess) {
  // Arrange
  RegisterUserResponse resp;

  auto stub = std::make_shared<MockGolfStub>();
  ON_CALL(*stub, RegisterUser(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  GolfClient client(stub);

  // Act
  auto status = client.RegisterUser("Tippy");

  // Assert
  EXPECT_TRUE(status.ok());
}

TEST(GolfClient, RegisterUserRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockGolfStub>();
  ON_CALL(*stub, RegisterUser(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));
  GolfClient client(stub);

  // Act
  auto status = client.RegisterUser("Tippy");

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kCancelled);
}

TEST(GolfClient, NewGameRpcSuccess) {
  // Arrange
  NewGameResponse resp;

  auto stub = std::make_shared<MockGolfStub>();
  ON_CALL(*stub, NewGame(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  GolfClient client(stub);

  // Act
  auto status_or_game = client.NewGame("Tippy", 2);

  // Assert
  EXPECT_TRUE(status_or_game.ok());
}

TEST(GolfClient, NewGameRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockGolfStub>();
  ON_CALL(*stub, NewGame(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));
  GolfClient client(stub);

  // Act
  auto status_or_game = client.NewGame("Tippy", 2);

  // Assert
  EXPECT_FALSE(status_or_game.ok());
  EXPECT_EQ(status_or_game.status().code(), absl::StatusCode::kCancelled);
}
