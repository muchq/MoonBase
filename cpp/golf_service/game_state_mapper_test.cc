#include "cpp/golf_service/game_state_mapper.h"

#include <gtest/gtest.h>

#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

TEST(GameStateMapper, GameStateToString) {
  GameStateMapper gsm;
  std::deque<Card> drawPile{5};
  std::deque<Card> discardPile{6};
  std::vector<Player> players{{"andy", 0, 1, 2, 3}};

  GameStatePtr state =
      std::make_shared<GameState>(GameState{drawPile, discardPile, players, false, 0, -1, "foo"});

  std::string expected(R"({"allHere":true,"discardSize":1,"drawSize":1,"gameId":"foo",)");
  expected.append(R"("gameOver":false,"hand":["2_C","3_D","4_H","5_S"],)");
  expected.append(R"("numberOfPlayers":1,"topDiscard":"8_H","yourTurn":true)");
  expected.append("}");

  EXPECT_EQ(gsm.gameStateJson(state, "andy"), expected);
}
