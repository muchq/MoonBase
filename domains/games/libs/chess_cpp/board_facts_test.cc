#include "domains/games/libs/chess_cpp/board_facts.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "chess.hpp"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp::facts {
namespace {

/// The squares of a bitboard, sorted, as "a1"-style names — bitboards make
/// unreadable failure messages.
std::vector<std::string> Squares(chess::Bitboard bitboard) {
  std::vector<std::string> names;
  while (bitboard) {
    names.push_back(chess::Square(bitboard.pop()));
  }
  std::sort(names.begin(), names.end());
  return names;
}

chess::Square Sq(std::string_view name) { return chess::Square(name); }

// --- Checkers -----------------------------------------------------------

TEST(Checkers, EmptyWhenNotInCheck) {
  const chess::Board board("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  EXPECT_THAT(Squares(Checkers(board, Side::kBlack)), testing::IsEmpty());
  EXPECT_THAT(Squares(Checkers(board, Side::kWhite)), testing::IsEmpty());
}

TEST(Checkers, NamesTheCheckingPiece) {
  // White rook on e2, Black king on e8, nothing between.
  const chess::Board board("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1");
  EXPECT_THAT(Squares(Checkers(board, Side::kBlack)), testing::ElementsAre("e2"));
  EXPECT_FALSE(InDoubleCheck(board, Side::kBlack));
}

TEST(Checkers, NamesBothPiecesInADoubleCheck) {
  // Knight on f6 and rook on e1 both bear on e8 — the position no query
  // over the Java pipeline's occurrence rows can recognise directly.
  const chess::Board board("4k3/8/5N2/8/8/8/8/4RK2 b - - 0 1");
  EXPECT_THAT(Squares(Checkers(board, Side::kBlack)), testing::ElementsAre("e1", "f6"));
  EXPECT_TRUE(InDoubleCheck(board, Side::kBlack));
}

// --- AttackersOf --------------------------------------------------------

TEST(AttackersOf, FindsEveryAttackerOfASquare) {
  // Both knights bear on d6; the black pawn on d5 bears on c4 and e4.
  const chess::Board board("4k3/8/8/3p4/2N1N3/8/8/4K3 w - - 0 1");
  EXPECT_THAT(Squares(AttackersOf(board, Side::kWhite, Sq("d6"))),
              testing::ElementsAre("c4", "e4"));
  EXPECT_THAT(Squares(AttackersOf(board, Side::kBlack, Sq("e4"))), testing::ElementsAre("d5"));
}

TEST(AttackersOf, RespectsBlockers) {
  // Two rooks stacked on the a-file: only the front one attacks a8. The
  // one behind is an x-ray, and a caller that wants it has to ask again
  // with the blocker gone.
  const chess::Board board("4k3/8/8/8/8/R7/R7/4K3 w - - 0 1");
  EXPECT_THAT(Squares(AttackersOf(board, Side::kWhite, Sq("a8"))), testing::ElementsAre("a3"));
}

// --- Between / Aligned --------------------------------------------------

TEST(Between, SpansARankAFileAndADiagonal) {
  EXPECT_THAT(Squares(Between(Sq("a1"), Sq("e1"))), testing::ElementsAre("b1", "c1", "d1"));
  EXPECT_THAT(Squares(Between(Sq("d1"), Sq("d4"))), testing::ElementsAre("d2", "d3"));
  EXPECT_THAT(Squares(Between(Sq("a1"), Sq("d4"))), testing::ElementsAre("b2", "c3"));
}

TEST(Between, IsSymmetric) {
  EXPECT_EQ(Squares(Between(Sq("c3"), Sq("f6"))), Squares(Between(Sq("f6"), Sq("c3"))));
}

TEST(Between, IsEmptyForAdjacentUnalignedAndIdenticalSquares) {
  EXPECT_THAT(Squares(Between(Sq("a1"), Sq("a2"))), testing::IsEmpty()) << "adjacent";
  EXPECT_THAT(Squares(Between(Sq("a1"), Sq("b3"))), testing::IsEmpty()) << "knight-apart";
  EXPECT_THAT(Squares(Between(Sq("d4"), Sq("d4"))), testing::IsEmpty()) << "same square";
}

TEST(Between, ExcludesBothEndpoints) {
  const std::vector<std::string> squares = Squares(Between(Sq("a1"), Sq("h8")));
  EXPECT_THAT(squares, testing::Not(testing::Contains("a1")));
  EXPECT_THAT(squares, testing::Not(testing::Contains("h8")));
  EXPECT_EQ(squares.size(), 6u);
}

TEST(Aligned, HoldsExactlyForSharedRankFileOrDiagonal) {
  EXPECT_TRUE(Aligned(Sq("a1"), Sq("h1")));
  EXPECT_TRUE(Aligned(Sq("c2"), Sq("c7")));
  EXPECT_TRUE(Aligned(Sq("a1"), Sq("h8")));
  EXPECT_FALSE(Aligned(Sq("a1"), Sq("b3")));
  EXPECT_FALSE(Aligned(Sq("d4"), Sq("d4"))) << "a square is not aligned with itself";
}

// --- SideToMoveKingEscapes --------------------------------------------------------

TEST(SideToMoveKingEscapes, ListsTheSquaresTheKingMayLegallyReach) {
  const chess::Board board("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  EXPECT_THAT(Squares(SideToMoveKingEscapes(board)),
              testing::ElementsAre("d1", "d2", "e2", "f1", "f2"));
}

TEST(SideToMoveKingEscapes, IsEmptyWhenTheKingIsSmotheredByItsOwnPieces) {
  // The knight mates on f7; every square the king could use is held by its
  // own rook and pawns. "No escapes because they are all mine" is what
  // separates a smothered mate from any other mate, and it is a question
  // about legal moves, not about placement.
  const chess::Board board("6rk/5Npp/8/8/8/8/8/6K1 b - - 0 1");
  EXPECT_THAT(Squares(SideToMoveKingEscapes(board)), testing::IsEmpty());
  EXPECT_EQ(board.isGameOver().first, chess::GameResultReason::CHECKMATE);
}

TEST(SideToMoveKingEscapes, OmitsSquaresCoveredByTheEnemy) {
  // Black king on e8, White rook on d1 sealing the d-file.
  const chess::Board board("4k3/8/8/8/8/8/8/3R1K2 b - - 0 1");
  EXPECT_THAT(Squares(SideToMoveKingEscapes(board)), testing::ElementsAre("e7", "f7", "f8"));
}

// --- ClassifyCheck ------------------------------------------------------

TEST(ClassifyCheck, ReportsNoCheckForAQuietMove) {
  const chess::Board board("4k3/8/8/8/8/8/R7/4K3 w - - 0 1");
  EXPECT_EQ(ClassifyCheck(board, chess::uci::parseSan(board, "Ra3")), CheckKind::kNone);
}

TEST(ClassifyCheck, ReportsDirectWhenTheMovedPieceGivesIt) {
  const chess::Board board("4k3/8/8/8/8/8/R7/4K3 w - - 0 1");
  EXPECT_EQ(ClassifyCheck(board, chess::uci::parseSan(board, "Ra8")), CheckKind::kDirect);
}

TEST(ClassifyCheck, ReportsDiscoveredWhenTheMoveUncoversAnotherPiece) {
  // The bishop steps off the e-file and the rook behind it does the
  // checking. The Java detectors infer this from where the moved piece
  // landed; here the move generator is asked outright.
  const chess::Board board("4k3/8/8/8/4B3/8/8/4RK2 w - - 0 1");
  EXPECT_EQ(ClassifyCheck(board, chess::uci::parseSan(board, "Bb1")), CheckKind::kDiscovered);
}

TEST(ClassifyCheck, ReportsDirectWhenCastlingLandsTheRookOnTheCheckingLine) {
  // The rook moved, so it discovered nothing — but upstream hardcodes
  // castling as a discovery, which would file every castles-with-check as
  // a DISCOVERED_CHECK. White castles long and the rook lands on d1,
  // checking the king on d8.
  const chess::Board board("3k4/8/8/8/8/8/8/R3K3 w Q - 0 1");
  EXPECT_EQ(ClassifyCheck(board, chess::uci::parseSan(board, "O-O-O")), CheckKind::kDirect);
}

TEST(ClassifyCheck, ReportsNoCheckForCastlingThatChecksNothing) {
  const chess::Board board("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
  EXPECT_EQ(ClassifyCheck(board, chess::uci::parseSan(board, "O-O-O")), CheckKind::kNone);
}

TEST(ClassifyCheck, ReportsDirectForADoubleCheckAndLeavesTheCountingToCheckers) {
  // A move can be direct and discovered at once; upstream answers with the
  // first kind it tests, so this says kDirect. The double is Checkers'
  // question, not this one — the header says so, and this is why.
  const chess::Board board("4k3/8/8/8/4N3/8/8/4RK2 w - - 0 1");
  const chess::Move move = chess::uci::parseSan(board, "Nf6");
  EXPECT_EQ(ClassifyCheck(board, move), CheckKind::kDirect);

  chess::Board after = board;
  after.makeMove(move);
  EXPECT_EQ(Checkers(after, Side::kBlack).count(), 2) << "knight and the uncovered rook";
  EXPECT_TRUE(InDoubleCheck(after, Side::kBlack));
}

}  // namespace
}  // namespace chess_cpp::facts
