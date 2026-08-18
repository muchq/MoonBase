#include "domains/games/libs/one_d4_motifs/board_scan.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "chess.hpp"
#include "domains/games/libs/chess_cpp/board_facts.h"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/chess_cpp/side.h"

namespace one_d4 {

int Value(chess::Piece piece) { return Value(piece.type()); }

bool SlidesAlong(chess::Piece piece, Direction direction) {
  switch (piece.type().internal()) {
    case chess::PieceType::underlying::QUEEN:
      return true;
    case chess::PieceType::underlying::BISHOP:
      return direction.IsDiagonal();
    case chess::PieceType::underlying::ROOK:
      return direction.IsStraight();
    default:
      return false;
  }
}

bool BelongsTo(chess::Piece piece, chess_cpp::Side side) {
  return piece != chess::Piece::NONE && piece.color() == chess_cpp::ToColor(side);
}

std::optional<int> CheapestAttackerValue(const chess::Board& board, chess_cpp::Side by,
                                         chess::Square square) {
  chess::Bitboard attackers = chess_cpp::facts::AttackersOf(board, by, square);
  if (!attackers) return std::nullopt;
  int cheapest = Value(chess::PieceType::KING);
  while (attackers) {
    cheapest = std::min(cheapest, Value(board.at(chess::Square(attackers.pop()))));
  }
  return cheapest;
}

std::optional<chess::Square> FirstInScanOrder(chess::Bitboard squares) {
  std::optional<chess::Square> first;
  ForEachSquare([&](int row, int col) {
    if (first.has_value()) return;
    const chess::Square square = SquareAt(row, col);
    if (squares.check(square.index())) first = square;
  });
  return first;
}

std::optional<chess::Square> FirstPieceAlong(const chess::Board& board, int row, int col,
                                             Direction direction) {
  int r = row + direction.dr;
  int c = col + direction.dc;
  while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
    const chess::Square square = SquareAt(r, c);
    if (board.at(square) != chess::Piece::NONE) return square;
    r += direction.dr;
    c += direction.dc;
  }
  return std::nullopt;
}

chess::Square CastledRookTo(chess::Square king_from, bool kingside) {
  return chess::Square(king_from.index() / 8 * 8 + (kingside ? 5 : 3));
}

std::vector<chess::Square> LandedOn(const chess_cpp::Position& position) {
  if (!position.last.has_value()) return {};
  const chess::Move move = position.last->move;
  if (move.typeOf() != chess::Move::CASTLING) return {move.to()};

  // Encoded king-takes-rook: from is the king, to is the rook it swaps with.
  const bool kingside = move.to() > move.from();
  const chess::Square king_to = chess::Square(move.from().index() / 8 * 8 + (kingside ? 6 : 2));
  return {king_to, CastledRookTo(move.from(), kingside)};
}

bool IsPromotion(const chess_cpp::Position& position) {
  return position.last.has_value() && position.last->move.typeOf() == chess::Move::PROMOTION;
}

bool IsCheckmate(const chess_cpp::Position& position) {
  // The definition, not isGameOver(): that tests the draws first, so a mate
  // reached on a threefold repetition is reported as the repetition.
  return position.board.inCheck() && !chess::movegen::anylegalmoves(position.board);
}

}  // namespace one_d4
