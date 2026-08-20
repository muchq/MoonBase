#include "domains/games/apis/one_d4_v2/analyze.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace one_d4_v2 {
namespace {

using ::testing::Contains;
using ::testing::Key;
using ::testing::Not;

constexpr char kScholarsMate[] =
    "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n"
    "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";

TEST(AnalyzeTest, FindsTheMotifsInAGame) {
  const auto analysis = Analyze(kScholarsMate);
  ASSERT_TRUE(analysis.ok()) << analysis.status();
  EXPECT_EQ(analysis->num_moves, 4);
  EXPECT_THAT(analysis->occurrences, Contains(Key("checkmate")));
  for (const auto& [motif, occurrences] : analysis->occurrences) {
    EXPECT_FALSE(occurrences.empty()) << motif << " is a key with nothing under it";
  }
}

TEST(AnalyzeTest, NamesMotifsInLowercaseLikeV1AlwaysHas) {
  const auto analysis = Analyze(kScholarsMate);
  ASSERT_TRUE(analysis.ok());
  for (const auto& [motif, occurrences] : analysis->occurrences) {
    EXPECT_EQ(motif, std::string(absl::AsciiStrToLower(motif)))
        << "v1 named motifs in lowercase, and the MCP tool parses that";
  }
}

TEST(AnalyzeTest, TheAttackPrimitiveIsNotACallerFacingMotif) {
  const auto analysis = Analyze(kScholarsMate);
  ASSERT_TRUE(analysis.ok());
  EXPECT_THAT(analysis->occurrences, Not(Contains(Key("attack"))))
      << "attack is what the detectors build on, not an answer";
}

TEST(AnalyzeTest, RejectsAMissingPgn) {
  EXPECT_EQ(Analyze("").status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(Analyze("   \n  ").status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(AnalyzeTest, RejectsAnOversizedPgnByBytesNotCharacters) {
  std::string huge(kMaxPgnBytes + 1, 'x');
  EXPECT_EQ(Analyze(huge).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(AnalyzeTest, RejectsAPgnThatWillNotParse) {
  const auto status = Analyze("this is not a pgn").status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(AnalyzeTest, RejectsMorePliesThanAnyChessGame) {
  // A syntactically valid game longer than the ply ceiling: knights out and
  // back, forever. Cheap to parse, expensive to analyze — which is the
  // exact lever the cap removes.
  std::string moves;
  for (int i = 1; moves.size() < 100 * 1024; ++i) {
    moves += absl::StrCat(i * 2 - 1, ". Nf3 Nf6 ", i * 2, ". Ng1 Ng8 ");
  }
  const std::string pgn = absl::StrCat("[Event \"x\"]\n\n", moves, "*\n");
  ASSERT_LE(pgn.size(), kMaxPgnBytes) << "the byte cap fired first, so this proves nothing";

  EXPECT_EQ(Analyze(pgn).status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace one_d4_v2
