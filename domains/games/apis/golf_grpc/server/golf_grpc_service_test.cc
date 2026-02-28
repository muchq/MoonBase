#include "domains/games/apis/golf_grpc/server/golf_grpc_service.h"

#include <google/protobuf/util/message_differencer.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include "domains/games/apis/golf_grpc/client/golf_grpc_client.h"
#include "domains/games/apis/golf_grpc/server/test_helpers.h"
#include "domains/games/libs/cards/golf/in_memory_game_store.h"
#include "domains/games/protos/golf_grpc/golf.grpc.pb.h"

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

TEST(SERVICE_TEST, JoinGame) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_one{"hello@example.org"};
  const std::string user_two{"bonk@example.org"};
  const int number_of_players = 2;

  // Act
  auto register_user_one_status = client.RegisterUser(user_one);
  auto register_user_two_status = client.RegisterUser(user_two);
  auto new_game_status_or_game = client.NewGame(user_one, number_of_players);
  const std::string& game_id = new_game_status_or_game->game_id();
  const std::string& version = new_game_status_or_game->version();

  auto join_game_status_or_game = client.JoinGame(user_two, game_id);

  // Assert
  EXPECT_TRUE(register_user_one_status.ok());
  EXPECT_TRUE(register_user_two_status.ok());
  EXPECT_TRUE(new_game_status_or_game.ok());
  EXPECT_TRUE(join_game_status_or_game.ok());

  const auto& game_state = join_game_status_or_game.value();

  golf_grpc::GameState expected_game_state;
  expected_game_state.set_all_here(true);
  expected_game_state.set_discard_size(1);
  expected_game_state.set_draw_size(43);
  expected_game_state.set_game_id(game_id);
  expected_game_state.set_version(version);
  expected_game_state.set_game_started(true);
  expected_game_state.set_game_over(false);
  expected_game_state.set_number_of_players(2);
  expected_game_state.add_players();
  expected_game_state.set_players(0, user_one);
  expected_game_state.add_players();
  expected_game_state.set_players(1, user_two);

  // 4 Aces from the top of the unshuffled deck to user_one
  // 4 Kings from the top of the unshuffled deck to user_two
  // Queen of Spades is flipped onto the discard pile at the start of the game
  auto top_discard = new cards_proto::Card;
  top_discard->set_rank(cards_proto::Rank::Queen);
  top_discard->set_suit(cards_proto::Suit::Spades);

  expected_game_state.set_allocated_top_discard(top_discard);
  expected_game_state.set_your_turn(false);

  google::protobuf::util::MessageDifferencer differencer;
  std::string report;

  differencer.ReportDifferencesToString(&report);
  auto expected_matches_actual = differencer.Compare(expected_game_state, game_state);
  if (!expected_matches_actual) {
    std::cout << "Messages differ:" << std::endl;
    std::cout << report << std::endl;
  }

  EXPECT_TRUE(expected_matches_actual);

  server->Shutdown();
}

TEST(SERVICE_TEST, JoinGameFailsIfUserIsNotRegistered) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_one{"hello@example.org"};
  const std::string user_two{"bonk@example.org"};
  const int number_of_players = 2;

  // Act
  auto register_user_one_status = client.RegisterUser(user_one);
  auto new_game_status_or_game = client.NewGame(user_one, number_of_players);
  const std::string& game_id = new_game_status_or_game->game_id();

  auto join_game_status_or_game = client.JoinGame(user_two, game_id);

  // Assert
  EXPECT_TRUE(register_user_one_status.ok());
  EXPECT_TRUE(new_game_status_or_game.ok());
  EXPECT_FALSE(join_game_status_or_game.ok());
  EXPECT_EQ(join_game_status_or_game.status().message(), "unknown user");

  server->Shutdown();
}

TEST(SERVICE_TEST, JoinGameFailsIfGameIsFull) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_one{"hello@example.org"};
  const std::string user_two{"bonk@example.org"};
  const std::string user_three{"bonk@example.org"};
  const int number_of_players = 2;

  // Act
  auto register_user_one_status = client.RegisterUser(user_one);
  auto register_user_two_status = client.RegisterUser(user_two);
  auto register_user_three_status = client.RegisterUser(user_three);
  auto new_game_status_or_game = client.NewGame(user_one, number_of_players);
  const std::string& game_id = new_game_status_or_game->game_id();

  auto user_two_join_game_status_or_game = client.JoinGame(user_two, game_id);
  auto user_three_join_game_status_or_game = client.JoinGame(user_three, game_id);

  // Assert
  EXPECT_TRUE(register_user_one_status.ok());
  EXPECT_TRUE(register_user_two_status.ok());
  EXPECT_TRUE(new_game_status_or_game.ok());
  EXPECT_TRUE(user_two_join_game_status_or_game.ok());
  EXPECT_FALSE(user_three_join_game_status_or_game.ok());
  EXPECT_EQ(user_three_join_game_status_or_game.status().message(), "no spots available");

  server->Shutdown();
}

TEST(SERVICE_TEST, Peek) {
  // Arrange
  auto service = MakeAllocatedGolfService();
  auto server = MakeAllocatedServer(service.get());

  auto channel = server->InProcessChannel({});
  auto stub = std::make_shared<Golf::Stub>(Golf::Stub(channel));
  auto client = golf_grpc::GolfClient{stub};

  const std::string user_one{"hello@example.org"};
  const std::string user_two{"bonk@example.org"};

  // Act
  auto register_status = client.RegisterUser(user_one);
  auto register_user_two_status = client.RegisterUser(user_two);
  auto new_game_status_or_game = client.NewGame(user_one, 2);
  auto join_game_status_or_game = client.JoinGame(user_two, new_game_status_or_game->game_id());
  auto peek_status_or_game = client.PeekAtDrawPile(user_one, new_game_status_or_game->game_id());

  const std::string& game_id = new_game_status_or_game->game_id();
  const std::string& version = new_game_status_or_game->version();

  // Assert
  EXPECT_TRUE(new_game_status_or_game.ok());
  EXPECT_TRUE(register_user_two_status.ok());
  EXPECT_TRUE(join_game_status_or_game.ok());
  EXPECT_TRUE(peek_status_or_game.ok());

  const auto& game_state = peek_status_or_game.value();

  golf_grpc::GameState expected_game_state;
  expected_game_state.set_all_here(true);
  expected_game_state.set_discard_size(1);
  expected_game_state.set_draw_size(43);
  expected_game_state.set_game_id(game_id);
  expected_game_state.set_version(version);
  expected_game_state.set_game_started(true);
  expected_game_state.set_game_over(false);
  expected_game_state.set_number_of_players(2);
  expected_game_state.add_players();
  expected_game_state.set_players(0, user_one);
  expected_game_state.add_players();
  expected_game_state.set_players(1, user_two);

  // 4 Aces from the top of the unshuffled deck to user_one
  // 4 Kings from the top of the unshuffled deck to user_two
  // Queen of Spades is flipped onto the discard pile at the start of the game
  auto top_discard = new cards_proto::Card;
  top_discard->set_rank(cards_proto::Rank::Queen);
  top_discard->set_suit(cards_proto::Suit::Spades);

  expected_game_state.set_allocated_top_discard(top_discard);

  // Queen of Hearts is now the top of the Draw Pile
  auto top_draw = new cards_proto::Card;
  top_draw->set_rank(cards_proto::Rank::Queen);
  top_draw->set_suit(cards_proto::Suit::Hearts);

  expected_game_state.set_allocated_top_draw(top_draw);
  expected_game_state.set_your_turn(true);

  google::protobuf::util::MessageDifferencer differencer;
  std::string report;

  differencer.ReportDifferencesToString(&report);
  auto expected_matches_actual = differencer.Compare(expected_game_state, game_state);
  if (!expected_matches_actual) {
    std::cout << "Messages differ:" << std::endl;
    std::cout << report << std::endl;
  }

  EXPECT_TRUE(expected_matches_actual);

  server->Shutdown();
}
