#include "domains/games/libs/one_d4_motifs/notation.h"

#include <cctype>
#include <optional>
#include <string>

#include "chess.hpp"

namespace one_d4 {
namespace {

char UppercaseLetter(chess::PieceType type) {
  switch (type.internal()) {
    case chess::PieceType::underlying::PAWN:
      return 'P';
    case chess::PieceType::underlying::KNIGHT:
      return 'N';
    case chess::PieceType::underlying::BISHOP:
      return 'B';
    case chess::PieceType::underlying::ROOK:
      return 'R';
    case chess::PieceType::underlying::QUEEN:
      return 'Q';
    case chess::PieceType::underlying::KING:
      return 'K';
    case chess::PieceType::underlying::NONE:
      return '?';
  }
  return '?';
}

}  // namespace

char PieceLetter(chess::Piece piece) {
  const char letter = UppercaseLetter(piece.type());
  if (piece.color() == chess::Color::BLACK) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  }
  return letter;
}

std::string SquareName(chess::Square square) {
  std::string name;
  name += static_cast<char>('a' + square.index() % 8);
  name += static_cast<char>('1' + square.index() / 8);
  return name;
}

std::string PieceNotation(chess::Piece piece, chess::Square square) {
  return PieceLetter(piece) + SquareName(square);
}

std::string MovedPieceNotation(chess::Piece piece, chess::Square from,
                               std::optional<chess::Square> to) {
  return PieceLetter(piece) + SquareName(from) + (to.has_value() ? SquareName(*to) : "??");
}

}  // namespace one_d4
