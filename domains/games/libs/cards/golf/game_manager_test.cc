#include "domains/games/libs/cards/golf/game_manager.h"

#include <gtest/gtest.h>

#include <vector>

#include "domains/games/libs/cards/golf/game_store.h"
#include "domains/games/libs/cards/golf/in_memory_game_store.h"

using namespace cards;
using namespace golf;

TEST(GameManager, Constructor) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};

  std::unordered_set<std::string> expectedUsers;
  EXPECT_EQ(gm.getUsersOnline(), expectedUsers);

  EXPECT_TRUE(gm.getGameIdsByUserId().empty());

  EXPECT_TRUE(gm.getGames().empty());
}

TEST(GameManager, RegisterUser) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto id = gm.registerUser("Andy");

  EXPECT_TRUE(id.ok());
  EXPECT_EQ(*id, "Andy");
}

TEST(GameManager, RegisterUserValidates) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("");
  EXPECT_FALSE(res1.ok());
  EXPECT_EQ(res1.status().message(), "username length must be between 4 and 40 chars");

  auto res2 = gm.registerUser("really_long_username_super_long_it_very_big_and_too_long");
  EXPECT_FALSE(res2.ok());
  EXPECT_EQ(res2.status().message(), "username length must be between 4 and 40 chars");

  auto res3 = gm.registerUser("weird%$name");
  EXPECT_FALSE(res3.ok());
  EXPECT_EQ(res3.status().message(),
            "only alphanumeric, underscore, @, dot, or dash allowed in username");
}

TEST(GameManager, RegisterUserNameTaken) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("foosername");
  EXPECT_TRUE(res1.ok());

  auto res2 = gm.registerUser("foosername");
  EXPECT_FALSE(res2.ok());
  EXPECT_EQ(res2.status().message(), "already exists");
}

TEST(GameManager, NewGame) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("user1");
  EXPECT_TRUE(res1.ok());

  auto res3 = gm.newGame("user1", 2);
  EXPECT_TRUE(res3.ok());
  auto gameState = res3->get();
  EXPECT_FALSE(gameState->getGameId().empty());
  EXPECT_EQ(gameState->getDrawPile().size(), 43);  // 8 cards for players, 1 in discard
  EXPECT_EQ(gameState->getDiscardPile().size(), 1);
  EXPECT_EQ(gameState->getPlayers().size(), 2);
  EXPECT_EQ(gameState->getWhoseTurn(), 0);
  EXPECT_EQ(gameState->getWhoKnocked(), -1);
  EXPECT_FALSE(gameState->isOver());
  EXPECT_FALSE(gameState->allPlayersPresent());
}

TEST(GameManager, NewGameWithWrongPlayerCount) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("user1");
  EXPECT_TRUE(res1.ok());

  auto res2 = gm.newGame("user1", 0);
  EXPECT_FALSE(res2.ok());
  EXPECT_EQ(res2.status().message(), "2 to 5 players");

  auto res3 = gm.newGame("user1", 6);
  EXPECT_FALSE(res3.ok());
  EXPECT_EQ(res3.status().message(), "2 to 5 players");
}

TEST(GameManager, NewGameWithUnknownUser) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res = gm.newGame("user1", 2);
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(res.status().message(), "unknown user");
}

TEST(GameManager, JoinGame) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("user1");
  EXPECT_TRUE(res1.ok());

  auto res2 = gm.newGame("user1", 2);
  EXPECT_TRUE(res2.ok());
  auto gameState = res2->get();

  auto res3 = gm.registerUser("user2");
  EXPECT_TRUE(res3.ok());

  auto res4 = gm.joinGame(gameState->getGameId(), "user2");
  EXPECT_TRUE(res4.ok());

  auto updatedGame = res4->get();
  EXPECT_TRUE(updatedGame->allPlayersPresent());

  std::unordered_set<std::string> expectedUsers{"user1", "user2"};
  auto playersInGame = gm.getUsersByGameId(updatedGame->getGameId());
  EXPECT_EQ(playersInGame, expectedUsers);
}

TEST(GameManager, Knock) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  auto res1 = gm.registerUser("user1");
  auto res2 = gm.newGame("user1", 2);
  EXPECT_TRUE(res2.ok());
  auto gameState = res2->get();
  auto res3 = gm.registerUser("user2");
  auto res4 = gm.joinGame(gameState->getGameId(), "user2");
  EXPECT_TRUE(res4.ok());
  auto updatedGame = res4->get();
  EXPECT_TRUE(updatedGame->allPlayersPresent());
  EXPECT_EQ(updatedGame->getWhoseTurn(), 0);

  auto bad_knock_status = gm.knock(updatedGame->getGameId(), "user2");
  EXPECT_FALSE(bad_knock_status.ok());

  auto good_knock_status = gm.knock(updatedGame->getGameId(), "user1");
  EXPECT_TRUE(good_knock_status.ok());

  auto after_knock = good_knock_status.value();
  EXPECT_EQ(after_knock->getWhoKnocked(), 0);
}

TEST(GameManager, GetGameStateForUser) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  std::string user_id = "user1";
  auto res1 = gm.registerUser(user_id);
  auto res2 = gm.newGame(user_id, 2);
  EXPECT_TRUE(res2.ok());
  auto game_id = res2->get()->getGameId();

  auto status_or_game = gm.getGameStateForUser(game_id, user_id);
  EXPECT_TRUE(status_or_game.ok());
}

TEST(GameManager, GetGameStateForIncorrectUser) {
  auto store = std::make_shared<InMemoryGameStore>();
  GameManager gm{store};
  std::string user_id = "user1";
  auto res1 = gm.registerUser(user_id);
  auto res2 = gm.newGame(user_id, 2);
  EXPECT_TRUE(res2.ok());
  auto game_id = res2->get()->getGameId();

  auto status_or_game = gm.getGameStateForUser(game_id, "fake_user");
  EXPECT_FALSE(status_or_game.ok());

  auto status = status_or_game.status();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "unknown user");
}
