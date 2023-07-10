#include "cpp/cards/golf/game_manager.h"

#include <gtest/gtest.h>

#include <vector>

#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

TEST(GameManager, Constructor) {
  GameManager gm;

  std::unordered_set<std::string> expectedUsers;
  EXPECT_EQ(gm.getUsersOnline(), expectedUsers);

  EXPECT_TRUE(gm.getGameIdsByUserId().empty());

  EXPECT_TRUE(gm.getGamesById().empty());
}

TEST(GameManager, RegisterUser) {
  GameManager gm;
  auto id = gm.registerUser("Andy");

  EXPECT_TRUE(id.ok());
  EXPECT_EQ(*id, "Andy");
}

TEST(GameManager, RegisterUserValidates) {
  GameManager gm;
  auto res1 = gm.registerUser("");
  EXPECT_FALSE(res1.ok());
  EXPECT_EQ(res1.status().message(), "username length must be between 4 and 15 chars");

  auto res2 = gm.registerUser("really_long_username");
  EXPECT_FALSE(res2.ok());
  EXPECT_EQ(res2.status().message(), "username length must be between 4 and 15 chars");

  auto res3 = gm.registerUser("weird%$name");
  EXPECT_FALSE(res3.ok());
  EXPECT_EQ(res3.status().message(), "only alphanumeric, underscore, or dash allowed in username");
}

TEST(GameManager, RegisterUserNameTaken) {
  GameManager gm;
  auto res1 = gm.registerUser("foosername");
  EXPECT_TRUE(res1.ok());

  auto res2 = gm.registerUser("foosername");
  EXPECT_FALSE(res2.ok());
  EXPECT_EQ(res2.status().message(), "username taken");
}

TEST(GameManager, NewGame) {
  GameManager gm;
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
  GameManager gm;
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
  GameManager gm;
  auto res = gm.newGame("user1", 2);
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(res.status().message(), "unregistered username");
}

TEST(GameManager, JoinGame) {
  GameManager gm;
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
