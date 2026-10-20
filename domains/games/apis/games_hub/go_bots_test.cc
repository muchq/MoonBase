#include "domains/games/apis/games_hub/go_bots.h"

#include <gtest/gtest.h>

namespace games_hub {
namespace {

TEST(GoBotLevel, ReadsOnlyALevelGnuGoHas) {
  EXPECT_EQ(GoBotLevel(GoBotId(1)), 1);
  EXPECT_EQ(GoBotLevel(GoBotId(10)), 10);
  EXPECT_EQ(GoBotLevel("gnugo@0"), std::nullopt);
  EXPECT_EQ(GoBotLevel("gnugo@11"), std::nullopt);
  EXPECT_EQ(GoBotLevel("gnugo@5x"), std::nullopt);
  EXPECT_EQ(GoBotLevel("stockfish@1500"), std::nullopt);
  EXPECT_EQ(GoBotLevel("alice"), std::nullopt);
}

// Columns skip I; rows count up from the bottom, points down from the top.
TEST(GtpVertex, NamesAPointAsGtpDoes) {
  EXPECT_EQ(GtpVertex(0, 9), "A9");
  EXPECT_EQ(GtpVertex(8, 9), "J9");
  EXPECT_EQ(GtpVertex(80, 9), "J1");
  EXPECT_EQ(GtpVertex(18 * 19 + 18, 19), "T1");
  EXPECT_EQ(GtpVertex(go::kPass, 19), "pass");
}

TEST(GtpPoint, ReadsAVertexBackInAnyCase) {
  for (const int size : {9, 13, 19}) {
    for (int point = 0; point < size * size; ++point) {
      EXPECT_EQ(GtpPoint(GtpVertex(point, size), size), point) << size << " " << point;
    }
  }
  EXPECT_EQ(GtpPoint("c3", 9), 6 * 9 + 2);
  EXPECT_EQ(GtpPoint("PASS", 9), go::kPass);
}

TEST(GtpPoint, RefusesWhatNamesNoPointOnTheBoard) {
  EXPECT_EQ(GtpPoint("I5", 19), std::nullopt);
  EXPECT_EQ(GtpPoint("K5", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("A10", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("A0", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("A", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("A5x", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("resign", 9), std::nullopt);
  EXPECT_EQ(GtpPoint("", 9), std::nullopt);
}

}  // namespace
}  // namespace games_hub
