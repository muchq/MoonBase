#include "domains/games/apis/games_hub/chess_bots.h"

#include <gtest/gtest.h>

namespace games_hub {
namespace {

TEST(ChessBotMovetimeMsTest, DefaultThreePlusTwoHitsTheCeiling) {
  // 180s / 60 + 0.8 * 2s = 3s + 1.6s, clamped to the ceiling.
  EXPECT_EQ(ChessBotMovetimeMs(180'000, 2'000), kChessBotMovetimeCeilMs);
}

TEST(ChessBotMovetimeMsTest, AShortClockStaysSnappy) {
  // 30s / 60 + 0 = 500 ms — between floor and ceiling.
  EXPECT_EQ(ChessBotMovetimeMs(30'000, 0), 500);
}

TEST(ChessBotMovetimeMsTest, IncrementCountsTowardTheBudget) {
  // 60s / 60 + 0.8 * 5s = 1s + 4s → ceiling.
  EXPECT_EQ(ChessBotMovetimeMs(60'000, 5'000), kChessBotMovetimeCeilMs);
  // 60s / 60 + 0 = 1s.
  EXPECT_EQ(ChessBotMovetimeMs(60'000, 0), 1'000);
}

TEST(ChessBotMovetimeMsTest, NeverBelowTheFloorOrAboveTheCeiling) {
  EXPECT_EQ(ChessBotMovetimeMs(1'000, 0), kChessBotMovetimeFloorMs);
  EXPECT_EQ(ChessBotMovetimeMs(3'600'000, 30'000), kChessBotMovetimeCeilMs);
}

TEST(ChessBotMovetimeMsTest, RemainingTimeCapsTheBudget) {
  // A 3+2 budget of 2s does not outrun a side down to 400 ms.
  EXPECT_EQ(ChessBotMovetimeMs(180'000, 2'000, 400), 400 - kChessBotClockReserveMs);
}

TEST(ChessBotMovetimeMsTest, ANearFlagSeatStillAsksTheEngineMinimum) {
  // Usable time at or below zero: the shortest the engine accepts, so the
  // ask itself cannot hang waiting on a zero movetime.
  EXPECT_EQ(ChessBotMovetimeMs(180'000, 2'000, kChessBotClockReserveMs),
            kChessBotMovetimeEngineMinMs);
  EXPECT_EQ(ChessBotMovetimeMs(180'000, 2'000, 0), kChessBotMovetimeEngineMinMs);
}

TEST(ChessBotIdTest, StrengthIsNamedInTheSeatId) {
  EXPECT_EQ(ChessBotId(1500), "stockfish@1500");
  EXPECT_EQ(ChessBotElo("stockfish@1500"), 1500);
  EXPECT_EQ(ChessBotElo("alice"), std::nullopt);
}

}  // namespace
}  // namespace games_hub
