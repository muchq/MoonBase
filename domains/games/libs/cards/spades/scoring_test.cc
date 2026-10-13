#include "domains/games/libs/cards/spades/scoring.h"

#include <gtest/gtest.h>

namespace spades {
namespace {

using Scores = std::vector<TeamScore>;

TEST(ScoreHand, AContractMadeScoresTenATrickAndABagATrickOver) {
  EXPECT_EQ(scoreHand({3, 2, 4, 2}, {3, 2, 4, 4}, {0, 0}), (Scores{{70, 0}, {42, 2}}));
}

TEST(ScoreHand, AContractSetLosesTenATrickBid) {
  EXPECT_EQ(scoreHand({3, 2, 4, 2}, {3, 4, 3, 3}, {0, 0}), (Scores{{-70, 0}, {43, 3}}));
}

// A nil bidder's tricks are bags, never the partner's contract.
TEST(ScoreHand, ANilScoresAHundredMadeAndLosesAHundredFailed) {
  EXPECT_EQ(scoreHand({kNil, 4, 4, 4}, {0, 4, 5, 4}, {0, 0}), (Scores{{141, 1}, {80, 0}}));
  EXPECT_EQ(scoreHand({kNil, 4, 4, 4}, {2, 3, 4, 4}, {0, 0}), (Scores{{-60, 2}, {-80, 0}}));
  EXPECT_EQ(scoreHand({kNil, 4, 4, 4}, {2, 4, 3, 4}, {0, 0}), (Scores{{-140, 2}, {80, 0}}));
}

TEST(ScoreHand, BothPartnersMayBidNil) {
  EXPECT_EQ(scoreHand({kNil, 6, kNil, 7}, {0, 6, 0, 7}, {0, 0}), (Scores{{200, 0}, {130, 0}}));
  EXPECT_EQ(scoreHand({kNil, 6, kNil, 6}, {0, 6, 1, 6}, {0, 0}), (Scores{{0, 1}, {120, 0}}));
}

TEST(ScoreHand, TenBagsCostAHundredAndAreCleared) {
  EXPECT_EQ(scoreHand({3, 2, 4, 2}, {3, 2, 4, 4}, {0, 8}), (Scores{{70, 0}, {-58, 0}}));
  EXPECT_EQ(scoreHand({3, 2, 4, 2}, {3, 2, 4, 4}, {0, 9}), (Scores{{70, 0}, {-58, 1}}));
  EXPECT_EQ(scoreHand({1, 1, 1, 1}, {13, 0, 0, 0}, {9, 0}), (Scores{{-169, 0}, {-20, 0}}));
}

TEST(GameWinner, TheHigherScoreOnceEitherTeamIsOutOrBust) {
  EXPECT_EQ(gameWinner({500, 300}), 0);
  EXPECT_EQ(gameWinner({520, 530}), 1);
  EXPECT_EQ(gameWinner({510, 510}), std::nullopt);
  EXPECT_EQ(gameWinner({-200, 100}), 1);
  EXPECT_EQ(gameWinner({-210, -250}), 0);
  EXPECT_EQ(gameWinner({499, -199}), std::nullopt);
}

// Cutthroat: a team of one a seat, each its own contract and bags.
TEST(ScoreHand, ATeamOfOneASeat) {
  EXPECT_EQ(scoreHand({3, kNil, 5}, {4, 0, 4}, {0, 0, 9}), (Scores{{31, 1}, {100, 0}, {-50, 9}}));
  EXPECT_EQ(scoreHand({3, 3}, {7, 6}, {0, 9}), (Scores{{34, 4}, {-67, 2}}));
}

TEST(GameWinner, TheHighestOnceAnyoneIsOutOrBust) {
  EXPECT_EQ(gameWinner({500, 300, 100}), 0);
  EXPECT_EQ(gameWinner({300, -200, 100}), 0);
  EXPECT_EQ(gameWinner({120, 530, 510, 90}), 1);
  EXPECT_EQ(gameWinner({510, 510, 300}), std::nullopt);
  EXPECT_EQ(gameWinner({499, 0, -199}), std::nullopt);
}

}  // namespace
}  // namespace spades
