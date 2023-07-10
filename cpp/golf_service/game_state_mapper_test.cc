#include "cpp/golf_service/game_state_mapper.h"

#include <gtest/gtest.h>

#include "cpp/cards/card.h"

using namespace cards;
using namespace golf;

TEST(GameStateMapper, GameStateToString) {
  GameStateMapper gsm;
  Card c(Suit::Clubs, Rank::Two);
  EXPECT_EQ(gsm.gameStateToString(nullptr), "2_C");
}
