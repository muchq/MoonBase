#include "domains/games/libs/one_d4_motifs/notation.h"

#include <gtest/gtest.h>

#include "chess.hpp"

namespace one_d4 {
namespace {

TEST(Notation, NamesEverySquare) {
  EXPECT_EQ(SquareName(chess::Square("a1")), "a1");
  EXPECT_EQ(SquareName(chess::Square("h8")), "h8");
  EXPECT_EQ(SquareName(chess::Square("e4")), "e4");
}

TEST(Notation, CaseCarriesTheColor) {
  const chess::Board board("4k3/8/8/8/8/8/8/4K2Q w - - 0 1");
  EXPECT_EQ(PieceNotation(board.at(chess::Square("h1")), chess::Square("h1")), "Qh1");
  EXPECT_EQ(PieceNotation(board.at(chess::Square("e8")), chess::Square("e8")), "ke8");
}

TEST(Notation, MovedPieceCarriesBothSquares) {
  const chess::Board board;
  EXPECT_EQ(
      MovedPieceNotation(board.at(chess::Square("g1")), chess::Square("g1"), chess::Square("f3")),
      "Ng1f3");
}

TEST(Notation, APromotionHasNoDestinationInThisSpelling) {
  // The pawn that left is not the piece that arrived, so there is nothing
  // to name. Stored rows carry the literal "??".
  const chess::Board board;
  EXPECT_EQ(MovedPieceNotation(board.at(chess::Square("e2")), chess::Square("e7"), std::nullopt),
            "Pe7??");
}

}  // namespace
}  // namespace one_d4
