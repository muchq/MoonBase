#include "domains/games/libs/chess_cpp/board_facts.h"

#include "chess.hpp"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp::facts {

chess::Bitboard Checkers(const chess::Board& board, Side side) {
  const chess::Color color = ToColor(side);
  return AttackersOf(board, Opponent(side), board.kingSq(color));
}

bool InDoubleCheck(const chess::Board& board, Side side) {
  return Checkers(board, side).count() >= 2;
}

chess::Bitboard AttacksFrom(const chess::Board& board, chess::Square square) {
  const chess::Piece piece = board.at(square);
  switch (piece.type().internal()) {
    case chess::PieceType::underlying::PAWN:
      return chess::attacks::pawn(piece.color(), square);
    case chess::PieceType::underlying::KNIGHT:
      return chess::attacks::knight(square);
    case chess::PieceType::underlying::BISHOP:
      return chess::attacks::bishop(square, board.occ());
    case chess::PieceType::underlying::ROOK:
      return chess::attacks::rook(square, board.occ());
    case chess::PieceType::underlying::QUEEN:
      return chess::attacks::queen(square, board.occ());
    case chess::PieceType::underlying::KING:
      return chess::attacks::king(square);
    case chess::PieceType::underlying::NONE:
      return chess::Bitboard{};
  }
  return chess::Bitboard{};
}

chess::Bitboard AttackersOf(const chess::Board& board, Side by, chess::Square square) {
  return chess::attacks::attackers(board, ToColor(by), square);
}

bool Aligned(chess::Square a, chess::Square b) {
  if (a == b) return false;
  const chess::Bitboard target = chess::Bitboard::fromSquare(b);
  return static_cast<bool>(chess::attacks::rook(a, target) & target) ||
         static_cast<bool>(chess::attacks::bishop(a, target) & target);
}

chess::Bitboard Between(chess::Square from, chess::Square to) {
  if (from == to) return {};

  // Sliding attacks stop *on* a blocker and include it. So the ray from
  // `from` with only `to` occupied covers everything up to and including
  // `to`, and the ray back from `to` with only `from` occupied covers
  // everything up to and including `from`. The two overlap exactly on the
  // squares in between: each endpoint is missing from its own ray, and the
  // other rays out of the two squares cannot meet twice.
  const chess::Bitboard from_bb = chess::Bitboard::fromSquare(from);
  const chess::Bitboard to_bb = chess::Bitboard::fromSquare(to);

  const chess::Bitboard rook_ray = chess::attacks::rook(from, to_bb);
  if (rook_ray & to_bb) return rook_ray & chess::attacks::rook(to, from_bb);

  const chess::Bitboard bishop_ray = chess::attacks::bishop(from, to_bb);
  if (bishop_ray & to_bb) return bishop_ray & chess::attacks::bishop(to, from_bb);

  return {};
}

chess::Bitboard SideToMoveKingEscapes(const chess::Board& board) {
  chess::Movelist moves;
  chess::movegen::legalmoves(moves, board, chess::PieceGenType::KING);

  chess::Bitboard squares;
  for (const chess::Move& move : moves) {
    // Castling is generated as king-takes-rook, so its target square is the
    // rook's, not the king's. It is also never a way out of check, which is
    // what callers are asking about, so it is left out rather than
    // translated.
    if (move.typeOf() == chess::Move::CASTLING) continue;
    squares |= chess::Bitboard::fromSquare(move.to());
  }
  return squares;
}

CheckKind ClassifyCheck(const chess::Board& board, chess::Move move) {
  // Castling: upstream hardcodes DISCOVERY_CHECK for it, which is wrong
  // whenever the rook itself lands on the checking line — the usual case,
  // and the one that would otherwise file a DISCOVERED_CHECK for every
  // castles-with-check in the corpus. Answered by looking at who is giving
  // check afterwards. Worth the board copy: a game has at most two of
  // these, against ~85 plies that take the branch below.
  if (move.typeOf() == chess::Move::CASTLING) {
    const Side moving = FromColor(board.sideToMove());
    chess::Board after = board;
    after.makeMove(move);

    const chess::Bitboard checkers = Checkers(after, Opponent(moving));
    if (!checkers) return CheckKind::kNone;

    // Castling is encoded king-takes-rook, so move.to() is where the rook
    // started, not where it ends: f-file when it started east of the king,
    // d-file when it started west.
    const bool kingside = move.to() > move.from();
    const chess::Square rook(kingside ? chess::File::FILE_F : chess::File::FILE_D,
                             move.to().rank());

    // The rook is the piece that moved, so a check from it is direct.
    // Anything else was uncovered by the king stepping off a line.
    return (checkers & ~chess::Bitboard::fromSquare(rook)) ? CheckKind::kDiscovered
                                                           : CheckKind::kDirect;
  }

  switch (board.givesCheck(move)) {
    case chess::CheckType::NO_CHECK:
      return CheckKind::kNone;
    case chess::CheckType::DIRECT_CHECK:
      return CheckKind::kDirect;
    case chess::CheckType::DISCOVERY_CHECK:
      return CheckKind::kDiscovered;
  }
  return CheckKind::kNone;
}

}  // namespace chess_cpp::facts
