#include "domains/games/apis/one_d4_worker/result.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace one_d4_worker {
namespace {

TEST(GameResult, ReadsAnExplicitWin) {
  EXPECT_EQ(ResultOf("win", "resigned"), "1-0");
  EXPECT_EQ(ResultOf("checkmated", "win"), "0-1");
}

TEST(GameResult, ReadsADrawFromEitherSide) {
  EXPECT_EQ(ResultOf("agreed", "agreed"), "1/2-1/2");
  EXPECT_EQ(ResultOf("repetition", "repetition"), "1/2-1/2");
  EXPECT_EQ(ResultOf("stalemate", "stalemate"), "1/2-1/2");
  EXPECT_EQ(ResultOf("insufficient", "insufficient"), "1/2-1/2");
  EXPECT_EQ(ResultOf("50move", "50move"), "1/2-1/2");
  EXPECT_EQ(ResultOf("timevsinsufficient", "timevsinsufficient"), "1/2-1/2");
  EXPECT_EQ(ResultOf("drawn", "drawn"), "1/2-1/2");
}

TEST(GameResult, InfersTheWinnerFromTheLoser) {
  // chess.com says why the loser lost and only "win" for the winner, but a
  // game can arrive with the loss recorded and the win missing.
  EXPECT_EQ(ResultOf("resigned", ""), "0-1");
  EXPECT_EQ(ResultOf("", "timeout"), "1-0");
  EXPECT_EQ(ResultOf("abandoned", ""), "0-1");
  EXPECT_EQ(ResultOf("", "checkmated"), "1-0");
  EXPECT_EQ(ResultOf("lose", ""), "0-1");
}

TEST(GameResult, SaysUnknownRatherThanGuessing) {
  EXPECT_EQ(ResultOf("", ""), "unknown");
  EXPECT_EQ(ResultOf("something new", "something newer"), "unknown");
}

// The vocabulary is chess.com's, and this is its only reader: the cases
// above are the pin, and a word missing from them is a game whose result
// the index silently calls unknown.

}  // namespace
}  // namespace one_d4_worker
