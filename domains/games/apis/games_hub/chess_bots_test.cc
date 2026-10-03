#include "domains/games/apis/games_hub/chess_bots.h"

#include <gtest/gtest.h>

namespace games_hub {
namespace {

// The UI's strength labels (1320…3190) name the seat; think time is the
// proxy that reaches Stockfish — more ms, stronger play at full strength.
TEST(ChessBotStrengthThinkMsTest, UiTiersMapToThinkTime) {
  EXPECT_EQ(ChessBotStrengthThinkMs(1320), 50);
  EXPECT_EQ(ChessBotStrengthThinkMs(1600), 150);
  EXPECT_EQ(ChessBotStrengthThinkMs(1900), 400);
  EXPECT_EQ(ChessBotStrengthThinkMs(2300), 1'200);
  EXPECT_EQ(ChessBotStrengthThinkMs(3190), 4'000);
}

TEST(ChessBotStrengthThinkMsTest, ValuesBetweenTiersInterpolate) {
  // Halfway from 1900/400ms to 2300/1200ms → 800 ms.
  EXPECT_EQ(ChessBotStrengthThinkMs(2100), 800);
}

TEST(ChessBotStrengthThinkMsTest, OutsideTheUiRangeClampsToTheEnds) {
  EXPECT_EQ(ChessBotStrengthThinkMs(1), 50);
  EXPECT_EQ(ChessBotStrengthThinkMs(10'000), 4'000);
}

TEST(ChessBotMovetimeMsTest, StrengthBudgetWhenTheClockHasRoom) {
  EXPECT_EQ(ChessBotMovetimeMs(/*remaining_ms=*/60'000, /*elo=*/2300), 1'200);
  EXPECT_EQ(ChessBotMovetimeMs(/*remaining_ms=*/60'000, /*elo=*/1320), 50);
}

TEST(ChessBotMovetimeMsTest, RemainingTimeCapsTheStrengthBudget) {
  EXPECT_EQ(ChessBotMovetimeMs(/*remaining_ms=*/400, /*elo=*/2300), 400 - kChessBotClockReserveMs);
}

TEST(ChessBotMovetimeMsTest, ANearFlagSeatStillAsksTheEngineMinimum) {
  EXPECT_EQ(ChessBotMovetimeMs(kChessBotClockReserveMs, 2300), kChessBotMovetimeEngineMinMs);
  EXPECT_EQ(ChessBotMovetimeMs(0, 2300), kChessBotMovetimeEngineMinMs);
}

TEST(ChessBotIdTest, StrengthIsNamedInTheSeatId) {
  EXPECT_EQ(ChessBotId(1500), "stockfish@1500");
  EXPECT_EQ(ChessBotElo("stockfish@1500"), 1500);
  EXPECT_EQ(ChessBotElo("alice"), std::nullopt);
}

}  // namespace
}  // namespace games_hub
