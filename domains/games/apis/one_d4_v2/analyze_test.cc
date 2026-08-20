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

// FORK is the one motif extraction never writes: it is a grouping of
// ATTACK rows by attacker, derived at read time in SQL for the query path
// and in PositionAnalyzer for /v1/analyze. Skipping the attack primitive
// without doing that derivation here silently deleted fork from analyze —
// while motif(fork) still finds it, which reads as a query bug.
TEST(AnalyzeTest, DerivesForkFromTheAttackRowsLikeEveryOtherReadPath) {
  // The Legal trap: 5. Nxf7 forks queen and rook.
  constexpr char kKnightFork[] =
      "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n\n"
      "1. e4 e5 2. Nf3 Nc6 3. Bc4 Nd4 4. Nxe5 Qg5 5. Nxf7 Qxg2 6. Rf1 Qxe4+ "
      "7. Be2 Nf3# 0-1\n";

  const auto analysis = Analyze(kKnightFork);
  ASSERT_TRUE(analysis.ok()) << analysis.status();
  ASSERT_TRUE(analysis->occurrences.count("fork"))
      << "5. Nxf7 forks queen and rook, and /v1/analyze says so";

  // The same rows the attack primitive carried: at least two, same ply,
  // same attacker, none discovered — a discovered attacker is not forking,
  // it is unmasking.
  const auto& forks = analysis->occurrences.at("fork");
  ASSERT_GE(forks.size(), 2u);
  for (const auto& fork : forks) {
    EXPECT_EQ(fork.ply, forks.front().ply);
    EXPECT_EQ(fork.attacker, forks.front().attacker);
    EXPECT_FALSE(fork.is_discovered);
  }
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

  const absl::Status status = Analyze(pgn).status();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()), ::testing::HasSubstr("plies"))
      << "rejected, but not by the cap this test is about";
}

}  // namespace
}  // namespace one_d4_v2
