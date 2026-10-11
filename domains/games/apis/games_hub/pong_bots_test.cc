#include "domains/games/apis/games_hub/pong_bots.h"

#include <gtest/gtest.h>

namespace games_hub {
namespace {

TEST(PongBots, AnIdNamesItsLevel) {
  EXPECT_EQ(PongBotId(2), "smallcat@2");
  for (int level = pong::kMinBotLevel; level <= pong::kMaxBotLevel; ++level) {
    EXPECT_EQ(PongBotLevel(PongBotId(level)), level);
  }
}

TEST(PongBots, NothingElseIsABot) {
  EXPECT_EQ(PongBotLevel("bouncy-coral-quokka-x9k2"), std::nullopt);
  EXPECT_EQ(PongBotLevel("stockfish@1500"), std::nullopt);
  EXPECT_EQ(PongBotLevel("smallcat@"), std::nullopt);
  EXPECT_EQ(PongBotLevel("smallcat@2x"), std::nullopt);
  EXPECT_EQ(PongBotLevel("smallcat@0"), std::nullopt);
  EXPECT_EQ(PongBotLevel("smallcat@4"), std::nullopt);
}

}  // namespace
}  // namespace games_hub
