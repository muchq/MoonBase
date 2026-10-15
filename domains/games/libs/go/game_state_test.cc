#include "domains/games/libs/go/game_state.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "absl/status/status.h"

namespace go {
namespace {

constexpr int64_t kT0 = 1'800'000'000'000;
constexpr TimeControl kMinute{60'000, 0};

GameState Start(int size = 9, double komi = kDefaultKomi, TimeControl tc = kMinute) {
  auto state = GameState::start({"ann", "bo"}, size, komi, 0, tc, kT0);
  EXPECT_TRUE(state.ok()) << state.status();
  return *state;
}

// Each point played in turn by whichever seat is on it.
GameState Played(GameState state, const std::vector<std::pair<int, int>>& points) {
  for (const auto& [x, y] : points) {
    auto next = state.play(state.whoseTurn(), x, y, kT0);
    EXPECT_TRUE(next.ok()) << "(" << x << "," << y << "): " << next.status();
    if (!next.ok()) return state;
    state = *std::move(next);
  }
  return state;
}

GameState Passed(GameState state, int passes = 2) {
  for (int i = 0; i < passes; ++i) state = *state.pass(state.whoseTurn(), kT0);
  return state;
}

TEST(GameState, StartsOnAnEmptyBoardBlackToMove) {
  const GameState state = Start(13);
  EXPECT_EQ(state.board(), std::string(13 * 13, kEmpty));
  EXPECT_EQ(state.sideToMove(), Color::kBlack);
  EXPECT_EQ(state.whoseTurn(), 0);
  EXPECT_EQ(state.colorOf(1), Color::kWhite);
  EXPECT_EQ(state.remainingMs(Color::kBlack, kT0), 60'000);
  EXPECT_FALSE(state.isOver());
}

TEST(GameState, RefusesABadSetup) {
  EXPECT_FALSE(GameState::start({"ann", "bo"}, 10, kDefaultKomi, 0, kMinute, kT0).ok());
  EXPECT_FALSE(GameState::start({"ann", "ann"}, 9, kDefaultKomi, 0, kMinute, kT0).ok());
  EXPECT_FALSE(GameState::start({"ann"}, 9, kDefaultKomi, 0, kMinute, kT0).ok());
  EXPECT_FALSE(GameState::start({"ann", "bo"}, 9, 6.25, 0, kMinute, kT0).ok());
  EXPECT_FALSE(GameState::start({"ann", "bo"}, 9, 150.5, 0, kMinute, kT0).ok());
  EXPECT_FALSE(GameState::start({"ann", "bo"}, 9, kDefaultKomi, 0, {0, 0}, kT0).ok());
  EXPECT_TRUE(GameState::start({"ann", "bo"}, 19, -7, 1, kMinute, kT0).ok());
}

TEST(GameState, AStoneWithoutLibertiesIsCaptured) {
  const GameState state = Played(Start(), {{1, 0}, {0, 0}, {0, 1}});
  EXPECT_EQ(state.at(0, 0), kEmpty);
  EXPECT_EQ(state.at(1, 0), kBlackStone);
  EXPECT_EQ(state.captures(Color::kBlack), 1);
  EXPECT_EQ(state.captures(Color::kWhite), 0);
  EXPECT_EQ(state.sideToMove(), Color::kWhite);
}

TEST(GameState, AGroupIsCapturedWhole) {
  // White's two stones on the edge lose their last liberty to (2,0).
  const GameState state = Played(Start(), {{0, 1}, {0, 0}, {1, 1}, {1, 0}, {2, 0}});
  EXPECT_EQ(state.at(0, 0), kEmpty);
  EXPECT_EQ(state.at(1, 0), kEmpty);
  EXPECT_EQ(state.captures(Color::kBlack), 2);
}

TEST(GameState, SuicideIsRefused) {
  const GameState state = Played(Start(), {{8, 8}, {1, 0}, {8, 7}, {0, 1}});
  const auto suicide = state.play(0, 0, 0, kT0);
  ASSERT_FALSE(suicide.ok());
  EXPECT_EQ(suicide.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(suicide.status().message(), "suicide is not allowed");
}

TEST(GameState, AnOccupiedPointOrOneOffTheBoardIsRefused) {
  const GameState state = Played(Start(), {{4, 4}});
  EXPECT_FALSE(state.play(1, 4, 4, kT0).ok());
  EXPECT_FALSE(state.play(1, 9, 0, kT0).ok());
  EXPECT_FALSE(state.play(1, -1, 0, kT0).ok());
  EXPECT_EQ(state.play(0, 3, 3, kT0).status().code(), absl::StatusCode::kFailedPrecondition);
}

// .bw.
// bw.w   black (2,1) takes (1,1); white may not take straight back.
// .bw.
GameState KoTaken() {
  return Played(Start(), {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}, {3, 1}, {8, 8}, {2, 2}, {2, 1}});
}

TEST(GameState, AKoCannotBeRetakenAtOnce) {
  const GameState taken = KoTaken();
  EXPECT_EQ(taken.at(1, 1), kEmpty);
  EXPECT_EQ(taken.captures(Color::kBlack), 1);
  const auto retake = taken.play(1, 1, 1, kT0);
  ASSERT_FALSE(retake.ok());
  EXPECT_EQ(retake.status().message(), "ko: retake elsewhere first");
}

TEST(GameState, AKoCanBeRetakenAfterAMoveElsewhere) {
  const GameState retaken = Played(KoTaken(), {{8, 0}, {8, 1}, {1, 1}});
  EXPECT_EQ(retaken.at(2, 1), kEmpty);
  EXPECT_EQ(retaken.at(1, 1), kWhiteStone);
  EXPECT_EQ(retaken.captures(Color::kWhite), 1);
}

TEST(GameState, TwoPassesInARowEndTheGame) {
  const GameState one = *Start().pass(0, kT0);
  EXPECT_FALSE(one.isOver());
  // A pass, a stone, a pass: not two in a row.
  const GameState apart = *Played(one, {{4, 4}}).pass(0, kT0);
  EXPECT_FALSE(apart.isOver());
  const GameState ended = *apart.pass(1, kT0);
  ASSERT_TRUE(ended.isOver());
  EXPECT_EQ(ended.result()->ending, Ending::kScore);
  EXPECT_EQ(ended.whoseTurn(), -1);
  EXPECT_FALSE(ended.pass(0, kT0).ok());
  EXPECT_FALSE(ended.play(0, 0, 0, kT0).ok());
}

TEST(GameState, AnEmptyBoardIsKomiForWhite) {
  const GameState ended = Passed(Start());
  EXPECT_EQ(ended.score(), (Score{0, 6.5}));
  EXPECT_EQ(ended.result(), (Result{Color::kWhite, Ending::kScore}));
}

TEST(GameState, AnEvenScoreIsADraw) {
  const GameState ended = Passed(Start(9, 0));
  EXPECT_EQ(ended.result(), (Result{std::nullopt, Ending::kScore}));
}

TEST(GameState, AreaCountsStonesAndTheEmptyPointsOnlyOneColourReaches) {
  // Black walls column 3, White column 5: column 4 touches both and is
  // nobody's.
  std::vector<std::pair<int, int>> walls;
  for (int y = 0; y < 9; ++y) {
    walls.emplace_back(3, y);
    walls.emplace_back(5, y);
  }
  const GameState ended = Passed(Played(Start(), walls));
  EXPECT_EQ(ended.score(), (Score{36, 42.5}));
  EXPECT_EQ(ended.result(), (Result{Color::kWhite, Ending::kScore}));

  const GameState no_komi = Passed(Played(Start(9, 0), walls));
  EXPECT_EQ(no_komi.result(), (Result{std::nullopt, Ending::kScore}));

  const GameState lone = Passed(Played(Start(), {{4, 4}}));
  EXPECT_EQ(lone.score(), (Score{81, 6.5}));
  EXPECT_EQ(lone.result(), (Result{Color::kBlack, Ending::kScore}));
}

TEST(GameState, EitherSeatMayResignOffTurn) {
  const auto resigned = Start().resign(1, kT0);
  ASSERT_TRUE(resigned.ok());
  EXPECT_EQ(resigned->result(), (Result{Color::kBlack, Ending::kResignation}));
}

TEST(GameState, AMoveChargesTheClockAndAddsTheIncrement) {
  const GameState start = Start(9, kDefaultKomi, {60'000, 5'000});
  const GameState moved = *start.play(0, 4, 4, kT0 + 1'000);
  EXPECT_EQ(moved.remainingMs(Color::kBlack, kT0 + 9'000), 64'000);
  EXPECT_EQ(moved.remainingMs(Color::kWhite, kT0 + 9'000), 52'000);
  const GameState passed = *moved.pass(1, kT0 + 3'000);
  EXPECT_EQ(passed.remainingMs(Color::kWhite, kT0 + 3'000), 63'000);
}

TEST(GameState, TheFlagFallsOnlyOnceTheTimeIsGone) {
  const GameState state = Start();
  EXPECT_EQ(state.flag(kT0 + 59'999).status().code(), absl::StatusCode::kFailedPrecondition);
  const auto flagged = state.flag(kT0 + 60'000);
  ASSERT_TRUE(flagged.ok());
  EXPECT_EQ(flagged->result(), (Result{Color::kWhite, Ending::kTimeout}));
  EXPECT_EQ(flagged->remainingMs(Color::kBlack, kT0 + 90'000), 0);
}

TEST(GameState, AMoveAfterTheFlagFellEndsTheGameOnTime) {
  const auto late = Start().play(0, 4, 4, kT0 + 60'000);
  ASSERT_TRUE(late.ok());
  EXPECT_EQ(late->result(), (Result{Color::kWhite, Ending::kTimeout}));
  EXPECT_EQ(late->at(4, 4), kEmpty);
}

TEST(GameState, ALeaveAbandonsUnlessTheFlagFellFirst) {
  EXPECT_EQ(Start().removePlayer(0, kT0)->result(), (Result{Color::kWhite, Ending::kAbandoned}));
  EXPECT_EQ(Start().removePlayer(1, kT0 + 60'000)->result(),
            (Result{Color::kWhite, Ending::kTimeout}));
}

TEST(GameState, RestoreReplaysTheMovesAndRefusesWhatPlayCouldNotReach) {
  const GameState taken = KoTaken();
  const auto back = GameState::restore(taken.players(), 9, kDefaultKomi, 0, taken.moves(), kMinute,
                                       taken.clock(), std::nullopt);
  ASSERT_TRUE(back.ok()) << back.status();
  EXPECT_EQ(back->board(), taken.board());
  EXPECT_EQ(back->captures(Color::kBlack), 1);

  std::vector<int> retake = taken.moves();
  retake.push_back(1 * 9 + 1);
  EXPECT_FALSE(GameState::restore(taken.players(), 9, kDefaultKomi, 0, retake, kMinute,
                                  taken.clock(), std::nullopt)
                   .ok());
  // A score without two passes, two passes without a score, the wrong
  // winner, and a timeout the clock does not show.
  EXPECT_FALSE(GameState::restore(taken.players(), 9, kDefaultKomi, 0, taken.moves(), kMinute,
                                  taken.clock(), Result{Color::kBlack, Ending::kScore})
                   .ok());
  const std::vector<int> passes{kPass, kPass};
  EXPECT_FALSE(GameState::restore({"ann", "bo"}, 9, kDefaultKomi, 0, passes, kMinute, taken.clock(),
                                  std::nullopt)
                   .ok());
  EXPECT_FALSE(GameState::restore({"ann", "bo"}, 9, kDefaultKomi, 0, passes, kMinute, taken.clock(),
                                  Result{Color::kBlack, Ending::kScore})
                   .ok());
  EXPECT_TRUE(GameState::restore({"ann", "bo"}, 9, kDefaultKomi, 0, passes, kMinute, taken.clock(),
                                 Result{Color::kWhite, Ending::kScore})
                  .ok());
  EXPECT_FALSE(GameState::restore({"ann", "bo"}, 9, kDefaultKomi, 0, {}, kMinute, taken.clock(),
                                  Result{Color::kWhite, Ending::kTimeout})
                   .ok());
}

}  // namespace
}  // namespace go
