#include "domains/games/libs/one_d4_motifs/extract.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/detector.h"
#include "domains/games/libs/one_d4_motifs/detectors.h"
#include "domains/games/libs/one_d4_motifs/motif.h"

namespace one_d4 {
namespace {

using ::testing::Contains;
using ::testing::HasSubstr;

GameFeatures Extracted(std::string_view pgn) {
  const auto game = chess_cpp::ParseGame(pgn);
  EXPECT_TRUE(game.ok()) << game.status();
  const auto features = Extract(*game);
  EXPECT_TRUE(features.ok()) << features.status();
  return *features;
}

constexpr char kScholarsMate[] = "[Event \"x\"]\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";

TEST(Extract, CountsMovesByTheLastMoveNumber) { EXPECT_EQ(Extracted(kScholarsMate).num_moves, 4); }

TEST(Extract, ReportsWhichMotifsFiredAtAll) {
  const GameFeatures features = Extracted(kScholarsMate);
  EXPECT_THAT(features.motifs, Contains(Motif::kCheck));
  EXPECT_THAT(features.motifs, Contains(Motif::kAttack));
  EXPECT_THAT(features.motifs, Contains(Motif::kPin));
  EXPECT_EQ(features.motifs.count(Motif::kPromotion), 0u);
}

TEST(Extract, EveryOccurrenceCarriesTheMotifItWasFiledUnder) {
  for (const MotifOccurrence& occurrence : Extracted(kScholarsMate).occurrences) {
    EXPECT_THAT(Extracted(kScholarsMate).motifs, Contains(occurrence.motif));
  }
}

// --- ply --------------------------------------------------------------

TEST(Extract, WhiteMovesAreOddPliesAndBlackEven) {
  // 3. Qh5 is White's third move, so ply 5; 3... Nf6 is Black's, ply 6.
  const GameFeatures features = Extracted(kScholarsMate);
  for (const MotifOccurrence& occurrence : features.occurrences) {
    const bool white = occurrence.side == chess_cpp::Side::kWhite;
    EXPECT_EQ(occurrence.ply, 2 * occurrence.move_number - (white ? 1 : 0))
        << "ply " << occurrence.ply << " on move " << occurrence.move_number;
    EXPECT_EQ(occurrence.ply % 2 == 1, white);
  }
}

TEST(Extract, RecordsBlacksFirstMove) {
  // The shape of the bug this found in the Java pipeline, since fixed: a
  // ply formula that subtracted one from an already-correct move number, so
  // 1... came out as ply 0 and was discarded as "the starting position".
  //
  // Black checks on move 1 from a position set up for it.
  const GameFeatures features = Extracted(
      "[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"3rk3/8/8/8/8/8/8/3RK3 b - - 0 1\"]\n\n1... Rxd1+ *\n");

  ASSERT_FALSE(features.occurrences.empty());
  for (const MotifOccurrence& occurrence : features.occurrences) {
    EXPECT_EQ(occurrence.ply, 2);
    EXPECT_EQ(occurrence.move_number, 1);
    EXPECT_EQ(occurrence.side, chess_cpp::Side::kBlack);
  }
}

// --- ordering ---------------------------------------------------------

TEST(Extract, GroupsOccurrencesByDetectorInTheOrderDetectorsRun) {
  // The order rows reach the API. Nothing else pins it — the parity golden
  // is sorted and compared as a multiset — so it is pinned against the
  // detector list itself rather than against a hand-written sequence.
  std::vector<Motif> expected;
  for (const std::unique_ptr<Detector>& detector : DefaultDetectors()) {
    expected.push_back(detector->motif());
  }

  const GameFeatures features = Extracted(kScholarsMate);
  std::vector<Motif> seen;
  for (const MotifOccurrence& occurrence : features.occurrences) {
    if (seen.empty() || seen.back() != occurrence.motif) seen.push_back(occurrence.motif);
  }
  ASSERT_FALSE(seen.empty());

  // seen must be expected with the silent detectors removed — same order,
  // no motif appearing twice.
  std::vector<Motif> fired;
  for (const Motif motif : expected) {
    if (features.motifs.count(motif) > 0) fired.push_back(motif);
  }
  EXPECT_EQ(seen, fired);
}

TEST(Extract, KeepsPlyOrderWithinAMotif) {
  const GameFeatures features = Extracted(kScholarsMate);
  int previous = 0;
  Motif motif = Motif::kPin;
  bool started = false;
  for (const MotifOccurrence& occurrence : features.occurrences) {
    if (!started || occurrence.motif != motif) {
      motif = occurrence.motif;
      previous = 0;
      started = true;
    }
    EXPECT_GE(occurrence.ply, previous);
    previous = occurrence.ply;
  }
}

// --- what OnGameEnd is given -------------------------------------------

/// What the extractor handed a detector.
struct Seen {
  int positions = 0;
  int game_ends = 0;
  int last_position_ply = -1;
  int final_ply = -1;
  std::string final_fen;
  std::string final_san;
};

class Recorder : public Detector {
 public:
  explicit Recorder(Seen* seen) : seen_(seen) {}

  Motif motif() const override { return Motif::kZugzwang; }

  void OnPosition(const chess_cpp::Position& position, Findings&) override {
    ++seen_->positions;
    seen_->last_position_ply = position.ply;
  }

  void OnGameEnd(const chess_cpp::Position& final_position, Findings&) override {
    ++seen_->game_ends;
    seen_->final_ply = final_position.ply;
    seen_->final_fen = final_position.board.getFen();
    seen_->final_san = std::string(final_position.last->san);
  }

 private:
  Seen* seen_;
};

Seen Record(std::string_view pgn) {
  Seen seen;
  std::vector<std::unique_ptr<Detector>> detectors;
  detectors.push_back(std::make_unique<Recorder>(&seen));

  const auto game = chess_cpp::ParseGame(pgn);
  EXPECT_TRUE(game.ok()) << game.status();
  if (game.ok()) {
    const auto features = Extract(*game, absl::MakeConstSpan(detectors));
    EXPECT_TRUE(features.ok()) << features.status();
  }
  return seen;
}

TEST(Extract, CallsOnGameEndOnceWithTheLastPosition) {
  const Seen seen = Record(kScholarsMate);
  EXPECT_EQ(seen.positions, 7);
  EXPECT_EQ(seen.game_ends, 1);
  EXPECT_EQ(seen.final_ply, seen.last_position_ply);
  EXPECT_EQ(seen.final_san, "Qxf7#");
}

TEST(Extract, TheFinalPositionSurvivesTheReplay) {
  // A copy, because the replayer's boards are gone by the time OnGameEnd
  // runs. Whether it is taken once or every ply is a cost, not a behaviour,
  // and nothing here can tell the difference.
  const Seen seen = Record(kScholarsMate);
  EXPECT_EQ(seen.final_fen, "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4");
}

TEST(Extract, CallsOnGameEndForAGameThatStartsFromAFenTag) {
  // The ply the copy is taken at counts half-moves in this replay, not in
  // the game, so a [FEN] start is the case that gets it wrong.
  const Seen seen = Record(
      "[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"3rk3/8/8/8/8/8/8/3RK3 b - - 0 40\"]\n\n"
      "40... Rxd1+ 41. Kxd1 *\n");
  EXPECT_EQ(seen.positions, 2);
  EXPECT_EQ(seen.game_ends, 1);
  EXPECT_EQ(seen.final_san, "Kxd1");
}

TEST(Extract, DoesNotCallOnGameEndForAGameWithNoMoves) {
  const Seen seen = Record("[Event \"abandoned\"]\n[Result \"*\"]\n\n*\n");
  EXPECT_EQ(seen.positions, 0);
  EXPECT_EQ(seen.game_ends, 0);
}

// --- games that are not the standard start ----------------------------

TEST(Extract, PlyFollowsTheMoveNumberEvenFromAMidGameFen) {
  // The replay has played one half-move; the game is on move 40. Ply is the
  // game's, or a stored row cannot be compared with any other.
  const GameFeatures features = Extracted(
      "[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"3rk3/8/8/8/8/8/8/3RK3 b - - 0 40\"]\n\n"
      "40... Rxd1+ *\n");
  ASSERT_FALSE(features.occurrences.empty());
  EXPECT_EQ(features.occurrences.front().ply, 80);
  EXPECT_EQ(features.occurrences.front().move_number, 40);
}

TEST(Extract, ReadsAnOddsGameFromItsFenTag) {
  // chess.com serves these with [SetUp "1"]; the Java pipeline replays them
  // from the standard position, fails on the first move, and indexes the
  // game as having no motifs at all.
  const GameFeatures features = Extracted(
      "[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"4k3/8/8/4n3/8/8/8/R6K w - - 0 1\"]\n\n1. Re1 *\n");
  EXPECT_THAT(features.motifs, Contains(Motif::kPin));
}

TEST(Extract, RejectsTagsThatNameNoPosition) {
  const auto game = chess_cpp::ParseGame("[Event \"x\"]\n[SetUp \"1\"]\n\n1. e4 *\n");
  ASSERT_TRUE(game.ok()) << game.status();
  const auto features = Extract(*game);
  EXPECT_EQ(features.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Extract, RejectsAGameThatWillNotReplay) {
  const auto game = chess_cpp::ParseGame("[Event \"x\"]\n\n1. e4 e5 2. Qh6 *\n");
  ASSERT_TRUE(game.ok()) << game.status();
  const auto features = Extract(*game);
  EXPECT_EQ(features.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(features.status().message(), HasSubstr("Qh6"));
}

TEST(Extract, FindsNothingInAGameWithNoMoves) {
  const GameFeatures features = Extracted("[Event \"abandoned\"]\n[Result \"*\"]\n\n*\n");
  EXPECT_TRUE(features.occurrences.empty());
  EXPECT_TRUE(features.motifs.empty());
  EXPECT_EQ(features.num_moves, 0);
}

}  // namespace
}  // namespace one_d4
