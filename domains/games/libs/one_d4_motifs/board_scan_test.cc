#include "domains/games/libs/one_d4_motifs/board_scan.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

#include "chess.hpp"
#include "domains/games/libs/one_d4_motifs/notation.h"

namespace one_d4 {
namespace {

TEST(BoardScan, ScanCoordinatesStartAtA8) {
  EXPECT_EQ(SquareAt(0, 0), chess::Square("a8"));
  EXPECT_EQ(SquareAt(7, 7), chess::Square("h1"));
  EXPECT_EQ(RowOf(chess::Square("a8")), 0);
  EXPECT_EQ(ColOf(chess::Square("h1")), 7);
}

TEST(BoardScan, VisitsSquaresInReadingOrder) {
  // The order stored rows come out in, so it is worth pinning by name.
  std::vector<std::string> visited;
  ForEachSquare([&](int row, int col) { visited.push_back(SquareName(SquareAt(row, col))); });
  ASSERT_EQ(visited.size(), 64u);
  EXPECT_EQ(visited.front(), "a8");
  EXPECT_EQ(visited[7], "h8");
  EXPECT_EQ(visited[8], "a7");
  EXPECT_EQ(visited.back(), "h1");
}

TEST(BoardScan, FirstInScanOrderPrefersTheHigherRank) {
  // Not lsb(): a bitboard's lowest bit is rank 1, and the Java pipeline
  // this has to match names the checker nearest rank 8 first.
  chess::Bitboard squares;
  squares.set(chess::Square("b1").index());
  squares.set(chess::Square("g7").index());
  EXPECT_EQ(FirstInScanOrder(squares), chess::Square("g7"));
}

TEST(BoardScan, FirstInScanOrderIsEmptyForNoSquares) {
  EXPECT_EQ(FirstInScanOrder(chess::Bitboard{}), std::nullopt);
}

TEST(BoardScan, StepsOffTheBoardAsNullopt) {
  EXPECT_EQ(Step(0, 0, Direction{-1, 0}), std::nullopt);
  EXPECT_EQ(Step(0, 0, Direction{1, 0}), chess::Square("a7"));
}

TEST(BoardScan, ValuesOrderThePieces) {
  EXPECT_EQ(Value(chess::PieceType::PAWN), 1);
  EXPECT_EQ(Value(chess::PieceType::ROOK), 4);
  EXPECT_EQ(Value(chess::PieceType::KING), 6);
  EXPECT_EQ(Value(chess::PieceType::NONE), 0);
}

TEST(BoardScan, SlidersMoveAlongTheirOwnLines) {
  const chess::Board board("4k3/8/8/8/8/8/8/RB2K2Q w - - 0 1");
  const Direction rank{0, 1};
  const Direction diagonal{1, 1};
  EXPECT_TRUE(SlidesAlong(board.at(chess::Square("a1")), rank));
  EXPECT_FALSE(SlidesAlong(board.at(chess::Square("a1")), diagonal));
  EXPECT_TRUE(SlidesAlong(board.at(chess::Square("b1")), diagonal));
  EXPECT_FALSE(SlidesAlong(board.at(chess::Square("b1")), rank));
  EXPECT_TRUE(SlidesAlong(board.at(chess::Square("h1")), rank));
  EXPECT_TRUE(SlidesAlong(board.at(chess::Square("h1")), diagonal));
  EXPECT_FALSE(SlidesAlong(board.at(chess::Square("e1")), rank)) << "a king slides nowhere";
}

TEST(BoardScan, AnEmptySquareBelongsToNobody) {
  const chess::Board board("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  EXPECT_FALSE(BelongsTo(board.at(chess::Square("d4")), chess_cpp::Side::kWhite));
  EXPECT_FALSE(BelongsTo(board.at(chess::Square("d4")), chess_cpp::Side::kBlack));
  EXPECT_TRUE(BelongsTo(board.at(chess::Square("e1")), chess_cpp::Side::kWhite));
}

TEST(BoardScan, FirstPieceAlongSkipsEmptySquares) {
  const chess::Board board("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
  EXPECT_EQ(FirstPieceAlong(board, RowOf(chess::Square("a1")), ColOf(chess::Square("a1")),
                            Direction{0, 1}),
            chess::Square("e1"));
  EXPECT_EQ(FirstPieceAlong(board, RowOf(chess::Square("a1")), ColOf(chess::Square("a1")),
                            Direction{-1, 0}),
            std::nullopt);
}

}  // namespace
}  // namespace one_d4
