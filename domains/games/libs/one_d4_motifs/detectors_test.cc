#include "domains/games/libs/one_d4_motifs/detectors.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/extract.h"
#include "domains/games/libs/one_d4_motifs/motif.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// A one-line PGN starting from `fen`. Constructed positions rather than
/// long move lists: a fixture that has to be played into is a fixture that
/// can be wrong about where the pieces are.
std::string From(std::string_view fen, std::string_view moves) {
  return absl::StrCat("[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"", fen, "\"]\n\n", moves, " *\n");
}

std::vector<MotifOccurrence> Found(std::string_view pgn, Motif motif) {
  const auto game = chess_cpp::ParseGame(pgn);
  EXPECT_TRUE(game.ok()) << game.status();
  if (!game.ok()) return {};
  const auto features = Extract(*game);
  EXPECT_TRUE(features.ok()) << features.status();
  if (!features.ok()) return {};

  std::vector<MotifOccurrence> matching;
  for (const MotifOccurrence& occurrence : features->occurrences) {
    if (occurrence.motif == motif) matching.push_back(occurrence);
  }
  return matching;
}

std::vector<std::string> Targets(const std::vector<MotifOccurrence>& occurrences) {
  std::vector<std::string> targets;
  for (const MotifOccurrence& occurrence : occurrences) {
    targets.push_back(occurrence.target.value_or("(none)"));
  }
  return targets;
}

// --- PIN ----------------------------------------------------------------

TEST(PinDetector, FindsAnAbsolutePinTheMoveCreated) {
  // Re1 puts the rook, the knight and the king on one file.
  const auto pins = Found(From("4k3/8/8/4n3/8/8/8/R6K w - - 0 1", "1. Re1"), Motif::kPin);
  ASSERT_EQ(pins.size(), 1u);
  EXPECT_EQ(pins[0].attacker, "Re1");
  EXPECT_EQ(pins[0].target, "ne5");
  EXPECT_EQ(pins[0].pin_type, PinType::kAbsolute);
  EXPECT_EQ(pins[0].ply, 1);
}

TEST(PinDetector, FindsARelativePinOntoARook) {
  // Bh1 pins the knight to the rook on a8 — legal to move, expensive to.
  const auto pins = Found(From("r2k4/8/8/3n4/8/8/6BK/8 w - - 0 1", "1. Bh1"), Motif::kPin);
  ASSERT_EQ(pins.size(), 1u);
  EXPECT_EQ(pins[0].attacker, "Bh1");
  EXPECT_EQ(pins[0].target, "nd5");
  EXPECT_EQ(pins[0].pin_type, PinType::kRelative);
}

TEST(PinDetector, IgnoresAPinItDidNotCreate) {
  // The rook already pinned the knight; shuffling the king is not news.
  EXPECT_THAT(Found(From("4k3/8/8/4n3/8/8/8/4R2K w - - 0 1", "1. Kg1"), Motif::kPin), IsEmpty());
}

TEST(PinDetector, IsNotFooledByABlocker) {
  // A pawn on e6 stands between the knight and its king.
  EXPECT_THAT(Found(From("4k3/8/4p3/4n3/8/8/8/R6K w - - 0 1", "1. Re1"), Motif::kPin), IsEmpty());
}

// --- CROSS_PIN ----------------------------------------------------------

TEST(CrossPinDetector, FindsAPieceHeldOnTwoLines) {
  // The knight on d5 is pinned to the king down the d-file and to the rook
  // on a8 along the long diagonal. The Java detector cannot see this — it
  // looks for one square found twice from the same king, which two rays
  // never do.
  const auto found = Found(From("r2k4/8/8/3n4/8/8/6BK/3R4 w - - 0 1", "1. Bh1"), Motif::kCrossPin);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].target, "nd5");
  EXPECT_EQ(found[0].attacker, "Bh1");
}

TEST(CrossPinDetector, OnePinIsNotACrossPin) {
  EXPECT_THAT(Found(From("4k3/8/8/4n3/8/8/8/R6K w - - 0 1", "1. Re1"), Motif::kCrossPin),
              IsEmpty());
}

// --- SKEWER -------------------------------------------------------------

TEST(SkewerDetector, FindsAKingSkeweredToARook) {
  const auto found = Found(From("4r3/8/8/4k3/8/8/8/R6K w - - 0 1", "1. Re1+"), Motif::kSkewer);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Re1");
  EXPECT_EQ(found[0].target, "ke5");
}

TEST(SkewerDetector, WillNotCallAPinASkewer) {
  // Cheap piece in front, valuable behind: that is a pin.
  EXPECT_THAT(Found(From("4k3/8/8/4n3/8/8/8/R6K w - - 0 1", "1. Re1"), Motif::kSkewer), IsEmpty());
}

// --- ATTACK -------------------------------------------------------------

TEST(AttackDetector, RecordsAnAttackOnTheQueen) {
  const auto found = Found(From("3qk3/8/8/8/8/8/8/3RK3 w - - 0 1", "1. Rd3"), Motif::kAttack);
  ASSERT_THAT(Targets(found), ElementsAre("qd8"));
  EXPECT_EQ(found[0].attacker, "Rd3");
  EXPECT_EQ(found[0].moved_piece, "Rd3");
  EXPECT_FALSE(found[0].is_discovered);
}

TEST(AttackDetector, IgnoresAnAttackOnAPawn) {
  // Only royalty, or two valuable pieces at once, earns a row.
  EXPECT_THAT(Found(From("4k3/3p4/8/8/8/8/8/3RK3 w - - 0 1", "1. Rd3"), Motif::kAttack), IsEmpty());
}

TEST(AttackDetector, RecordsEveryTargetOfAFork) {
  // Nc7+ hits the king and both rooks. The read path groups these rows
  // into FORK; extraction's job is to emit them all.
  const auto found = Found(
      From("r3k2r/8/8/8/8/8/8/4KN2 w - - 0 1", "1. Ne3 Kd8 2. Nd5 Ke8 3. Nc7+"), Motif::kAttack);
  EXPECT_THAT(Targets(found), ElementsAre("ke8", "ra8")) << "royalty first";
}

TEST(AttackDetector, RecordsWhatAMoveUncovered) {
  // The knight steps aside and the rook behind it bears on the queen.
  const auto found = Found(From("3qk3/8/8/8/8/3N4/8/3RK3 w - - 0 1", "1. Nf4"), Motif::kAttack);
  ASSERT_FALSE(found.empty());
  const MotifOccurrence& discovered = found.back();
  EXPECT_TRUE(discovered.is_discovered);
  EXPECT_EQ(discovered.moved_piece, "Nd3f4");
  EXPECT_EQ(discovered.attacker, "Rd1");
  EXPECT_EQ(discovered.target, "qd8");
}

// --- CHECK --------------------------------------------------------------

TEST(CheckDetector, NamesTheCheckingPieceAndTheKing) {
  const auto found = Found(From("4k3/8/8/8/8/8/8/3RK3 w - - 0 1", "1. Rd8+"), Motif::kCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Rd8");
  EXPECT_EQ(found[0].target, "ke8");
  EXPECT_FALSE(found[0].is_mate);
}

TEST(CheckDetector, MarksTheMatingCheck) {
  const auto found =
      Found("[Event \"x\"]\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n", Motif::kCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_TRUE(found[0].is_mate);
  EXPECT_EQ(found[0].description, "Checkmate at move 4");
  EXPECT_EQ(found[0].ply, 7);
}

TEST(CheckDetector, ReadsTheBoardRatherThanTheNotation) {
  // No '+' in the PGN. The Java detector fires on the suffix and would see
  // nothing here.
  const auto found = Found(From("4k3/8/8/8/8/8/8/3RK3 w - - 0 1", "1. Rd8"), Motif::kCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Rd8");
}

// --- PROMOTION ----------------------------------------------------------

TEST(PromotionDetector, FiresOnAPromotion) {
  const auto found = Found(From("8/P7/8/4k3/8/8/8/4K3 w - - 0 1", "1. a8=Q"), Motif::kPromotion);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].description, "Promotion at move 1");
  EXPECT_THAT(Found(From("8/P7/8/4k3/8/8/8/4K3 w - - 0 1", "1. a8=Q"), Motif::kPromotionWithCheck),
              IsEmpty());
}

TEST(PromotionWithCheckDetector, WantsThePromotedPieceToBeTheOneChecking) {
  const auto found =
      Found(From("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "1. a8=Q+"), Motif::kPromotionWithCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Qa8");
  EXPECT_EQ(found[0].target, "ke8");
}

TEST(PromotionWithCheckDetector, IgnoresACheckTheVacatedSquareUncovered) {
  // The pawn captures off the d-file and the rook behind it gives the
  // check; the new knight on c8 attacks no king at all.
  EXPECT_THAT(
      Found(From("2rk4/3P4/8/8/8/8/8/3RK3 w - - 0 1", "1. dxc8=N+"), Motif::kPromotionWithCheck),
      IsEmpty());
}

TEST(PromotionWithCheckmateDetector, FiresOnlyOnMate) {
  const auto found =
      Found(From("7k/5P2/6K1/8/8/8/8/8 w - - 0 1", "1. f8=Q#"), Motif::kPromotionWithCheckmate);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_TRUE(found[0].is_mate);
  EXPECT_EQ(found[0].attacker, "Qf8");
}

// --- MATES --------------------------------------------------------------

TEST(BackRankMateDetector, FindsAKingShutInByItsOwnPawns) {
  const auto found =
      Found(From("r6k/8/8/8/8/8/5PPP/6K1 b - - 0 1", "1... Ra1#"), Motif::kBackRankMate);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "ra1");
  EXPECT_EQ(found[0].target, "Kg1");
  EXPECT_TRUE(found[0].is_mate);
}

TEST(BackRankMateDetector, WantsTheKingsOwnPiecesInTheWay) {
  // Mate on the back rank, but the second rank is swept by the other rook
  // rather than blocked by the king's own men. A mate, not this one.
  EXPECT_THAT(Found(From("r3k3/8/8/8/8/8/r7/6K1 b - - 0 1", "1... Ra1#"), Motif::kBackRankMate),
              IsEmpty());
}

TEST(SmotheredMateDetector, WantsAKnightAndNoEscape) {
  const auto found =
      Found(From("6rk/6pp/8/6N1/8/8/8/6K1 w - - 0 1", "1. Nf7#"), Motif::kSmotheredMate);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Nf7");
  EXPECT_EQ(found[0].target, "kh8");
}

TEST(SmotheredMateDetector, WantsTheEscapeSquaresFilledByTheKingsOwnMen) {
  // A knight mate that is not smothered: the rook covers g7 and g8 rather
  // than the king's own pieces standing on them. Without this the escape
  // loop is never exercised in the direction that rejects.
  const std::string pgn = From("7k/7p/8/4N3/8/8/8/6RK w - - 0 1", "1. Nf7#");
  // It really is mate by a knight, or the detector would be bailing one
  // step earlier and this would pass without reaching the escape squares.
  const auto checks = Found(pgn, Motif::kCheck);
  ASSERT_EQ(checks.size(), 1u);
  ASSERT_TRUE(checks[0].is_mate);
  ASSERT_EQ(checks[0].attacker, "Nf7");

  EXPECT_THAT(Found(pgn, Motif::kSmotheredMate), IsEmpty());
}

TEST(SmotheredMateDetector, IgnoresAMateWithRoomToBreathe) {
  EXPECT_THAT(Found(From("7k/5P2/6K1/8/8/8/8/8 w - - 0 1", "1. f8=Q#"), Motif::kSmotheredMate),
              IsEmpty());
}

}  // namespace
}  // namespace one_d4
