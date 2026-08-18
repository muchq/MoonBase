#include "domains/games/libs/one_d4_motifs/detectors.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/side.h"
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

TEST(AttackDetector, SeesThroughTheSquareAnEnPassantCaptureEmpties) {
  // The taken pawn stood on neither square "exd6" names, and the bishop's
  // line to the rook ran through it. Missed for as long as the pipeline has
  // existed: the scan only visited squares the mover's own pieces left.
  const auto found =
      Found(From("r6k/3p4/8/4P3/8/8/6B1/6K1 b - - 0 1", "1... d5 2. exd6"), Motif::kAttack);
  ASSERT_FALSE(found.empty());
  const MotifOccurrence& discovered = found.back();
  EXPECT_TRUE(discovered.is_discovered);
  EXPECT_EQ(discovered.attacker, "Bg2");
  EXPECT_EQ(discovered.target, "ra8");
  EXPECT_EQ(discovered.moved_piece, "Pe5d6") << "the pawn that took, not the pawn taken";
}

TEST(AttackDetector, DoesNotCallAPromotedPieceADiscovery) {
  // The new queen stands one step up the file from the square the pawn
  // left, so a ray walking back from that square finds it and reports the
  // piece that just moved as one it uncovered.
  const auto found = Found(From("8/4P3/8/8/4r2k/8/8/6K1 w - - 0 1", "1. e8=Q"), Motif::kAttack);
  for (const MotifOccurrence& occurrence : found) {
    EXPECT_FALSE(occurrence.is_discovered)
        << occurrence.attacker.value_or("?") << " -> " << occurrence.target.value_or("?");
  }
}

TEST(AttackDetector, ReportsOneDiscoveryWhenAMoveEmptiesTwoSquaresOnOneRay) {
  // exd6+ empties e5 and d5, and both sit on the rook's line to the king.
  // Reported twice, the read path counts two attackers on one king and
  // derives a DOUBLE_CHECK that never happened.
  const auto found = Found(From("8/8/8/k2pP2R/8/8/8/7K w - d6 0 2", "2. exd6+"), Motif::kAttack);
  int on_the_king = 0;
  for (const MotifOccurrence& occurrence : found) {
    if (occurrence.attacker == "Rh5" && occurrence.target == "ka5") ++on_the_king;
  }
  EXPECT_EQ(on_the_king, 1);
}

TEST(AttackDetector, DoesNotCallTheCastledRookADiscovery) {
  // A ray out of the square the king left finds the rook on its new square,
  // and excluding only the destination paired with that origin lets it
  // through. The rook checks the king on b1 either way, so the row exists —
  // the question is whether it is reported twice, once as a discovery.
  const auto found = Found(From("8/8/8/8/8/8/8/nk2K2R w K - 0 1", "1. O-O+"), Motif::kAttack);
  int on_the_king = 0;
  for (const MotifOccurrence& occurrence : found) {
    if (occurrence.target != "kb1") continue;
    ++on_the_king;
    EXPECT_EQ(occurrence.attacker, "Rf1");
    EXPECT_FALSE(occurrence.is_discovered) << "the rook moved; it discovered nothing";
  }
  EXPECT_EQ(on_the_king, 1);
}

TEST(AttackDetector, RecordsWhatACastledRookAttacks) {
  // "O-O" names no square, so the rook's arrival on a new file used to
  // produce nothing at all — including when it is the mating move, which
  // left the derived CHECKMATE and DOUBLE_CHECK rows with no ATTACK to
  // derive from.
  const auto found =
      Found(From("5k2/4p1p1/3N4/8/8/8/B7/4K2R w K - 0 1", "1. O-O#"), Motif::kAttack);
  ASSERT_FALSE(found.empty());
  bool rook_on_the_king = false;
  for (const MotifOccurrence& occurrence : found) {
    if (occurrence.attacker == "Rf1" && occurrence.target == "kf8") {
      rook_on_the_king = true;
      EXPECT_TRUE(occurrence.is_mate);
      EXPECT_FALSE(occurrence.is_discovered) << "the rook moved; it discovered nothing";
    }
  }
  EXPECT_TRUE(rook_on_the_king);
}

TEST(PinDetector, SeesAPinTheCastledRookCreates) {
  const auto pins = Found(From("5k2/8/5n2/8/8/8/8/4K2R w K - 0 1", "1. O-O"), Motif::kPin);
  ASSERT_EQ(pins.size(), 1u);
  EXPECT_EQ(pins[0].attacker, "Rf1");
  EXPECT_EQ(pins[0].target, "nf6");
  EXPECT_EQ(pins[0].pin_type, PinType::kAbsolute);
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

// --- DISCOVERED_CHECK / DOUBLE_CHECK / CHECKMATE -------------------------

TEST(DiscoveredCheckDetector, NamesThePieceTheMoveUncovered) {
  // The knight steps off the e-file and the rook behind it gives check.
  // The read path infers this from a discovered ATTACK row that happens to
  // name a king; this asks the move generator what kind of check it is.
  const auto found =
      Found(From("4k3/8/8/8/4N3/8/8/4R2K w - - 0 1", "1. Nc5+"), Motif::kDiscoveredCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Re1");
  EXPECT_EQ(found[0].target, "ke8");
  EXPECT_EQ(found[0].moved_piece, "Ne4c5");
  EXPECT_TRUE(found[0].is_discovered);
}

TEST(DiscoveredCheckDetector, IgnoresACheckByThePieceThatMoved) {
  EXPECT_THAT(Found(From("4k3/8/8/8/8/8/8/3R3K w - - 0 1", "1. Re1+"), Motif::kDiscoveredCheck),
              IsEmpty());
}

TEST(DoubleCheckDetector, WantsTwoCheckersAtOnce) {
  // Nd6 checks from d6 and uncovers Re1 at the same time.
  const auto found =
      Found(From("4k3/8/8/8/4N3/8/8/4R2K w - - 0 1", "1. Nd6+"), Motif::kDoubleCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].target, "ke8");
  EXPECT_EQ(found[0].description, "Double check at move 1");
}

TEST(DiscoveredCheckDetector, FiresOnADoubleCheckToo) {
  // Nd6+ checks and uncovers Re1 at once. givesCheck reports the direct
  // half and stops, so a detector built on it alone records no discovery
  // for any double check — three of them in the parity corpus.
  const auto found =
      Found(From("4k3/8/8/8/4N3/8/8/4R2K w - - 0 1", "1. Nd6+"), Motif::kDiscoveredCheck);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Re1") << "the rook, not the knight that moved";
  EXPECT_EQ(found[0].moved_piece, "Ne4d6");
}

TEST(DoubleCheckDetector, OneCheckerIsNotADoubleCheck) {
  EXPECT_THAT(Found(From("4k3/8/8/8/4N3/8/8/4R2K w - - 0 1", "1. Nc5+"), Motif::kDoubleCheck),
              IsEmpty());
}

TEST(CheckmateDetector, StoresARowOfItsOwn) {
  // Derived from ATTACK rows until now, which is why ChessQL's ORDER BY and
  // sequence() have never matched a checkmate.
  const auto found =
      Found("[Event \"x\"]\n\n1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n", Motif::kCheckmate);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].attacker, "Qf7");
  EXPECT_EQ(found[0].target, "ke8");
  EXPECT_TRUE(found[0].is_mate);
  EXPECT_EQ(found[0].ply, 7);
}

TEST(CheckmateDetector, SaysNothingAboutAnUnfinishedGame) {
  EXPECT_THAT(Found("[Event \"x\"]\n\n1. e4 e5 *\n", Motif::kCheckmate), IsEmpty());
}

// --- ZUGZWANG / OVERLOADED_PIECE ----------------------------------------

TEST(ZugzwangDetector, FiresWhenEveryMoveLosesMaterial) {
  // Black's king is walled in by its own pawn and White's, and the knight
  // has nowhere to go that a pawn does not take it.
  const auto found =
      Found(From("6nk/6p1/6P1/6P1/2B5/8/4R3/K7 w - - 0 1", "1. Re1"), Motif::kZugzwang);
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0].target, "kh8");
  EXPECT_EQ(found[0].side, chess_cpp::Side::kWhite) << "attributed to whoever caused it";
}

TEST(ZugzwangDetector, IsNotCheck) {
  // Being in check is not being in zugzwang. Belt and braces: a king in
  // check is itself attacked and worth more than anything attacking it, so
  // the "nothing hanging yet" test would reject the position anyway.
  EXPECT_THAT(Found(From("4k3/8/8/8/8/8/8/3R3K w - - 0 1", "1. Re1+"), Motif::kZugzwang),
              IsEmpty());
}

TEST(ZugzwangDetector, IsNotAPositionWhereSomethingWasAlreadyHanging) {
  // If a piece is en prise before the move, losing it is not the obligation
  // to move — it is just a piece being en prise. Same wall, so every legal
  // move still costs and the position would otherwise qualify; what changes
  // is that the pawn on a7 is already hanging, blocked, and cannot be
  // saved.
  EXPECT_THAT(Found(From("1B4nk/p5p1/P5P1/6P1/8/8/4R3/K7 w - - 0 1", "1. Re1"), Motif::kZugzwang),
              IsEmpty());
}

TEST(OverloadedPieceDetector, FindsADefenderDoingTwoJobs) {
  // The rook on d7 is the only thing holding both the knight and the pawn.
  const auto found =
      Found(From("7k/3r1p2/8/3nP3/4P3/8/8/K7 w - - 0 1", "1. e6"), Motif::kOverloadedPiece);
  ASSERT_EQ(found.size(), 2u);
  for (const MotifOccurrence& occurrence : found) {
    EXPECT_EQ(occurrence.attacker, "rd7");
  }
  EXPECT_EQ(Targets(found), (std::vector<std::string>{"pf7", "nd5"}));
}

TEST(OverloadedPieceDetector, IgnoresADefenderWithOnlyOneJob) {
  EXPECT_THAT(Found(From("7k/3r4/8/3nP3/4P3/8/8/K7 w - - 0 1", "1. e6"), Motif::kOverloadedPiece),
              IsEmpty());
}

TEST(OverloadedPieceDetector, IgnoresADefenceNothingIsThreatening) {
  // Both black pieces are attacked and rd7 alone defends each, so this does
  // reach the two-duties test. It fails it because the rook cannot take the
  // pawn on f7 and profit — only the knight, attacked by a pawn, is really
  // being held. Counting defences like the f7 one makes half the board
  // overloaded: it is what took this detector from 200 rows to 2547 over
  // the parity corpus.
  EXPECT_THAT(
      Found(From("7k/3r1p2/8/3n4/4P3/8/8/R6K w - - 0 1", "1. Rf1"), Motif::kOverloadedPiece),
      IsEmpty());
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

TEST(BackRankMateDetector, WantsTheMateDeliveredAlongTheBackRank) {
  // Mated on its back rank, by a knight standing on the seventh. Mate on
  // the back rank is not a back-rank mate — ten of the parity corpus's
  // thirteen rows were this shape, mostly queens and bishops on g7.
  const std::string pgn = From("6rk/6pp/8/6N1/8/8/8/6K1 w - - 0 1", "1. Nf7#");
  const auto mates = Found(pgn, Motif::kCheckmate);
  ASSERT_EQ(mates.size(), 1u) << "the fixture has to be mate for this to mean anything";
  EXPECT_EQ(mates[0].attacker, "Nf7");
  EXPECT_THAT(Found(pgn, Motif::kBackRankMate), IsEmpty());
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
