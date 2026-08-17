#ifndef DOMAINS_GAMES_LIBS_CHESS_CPP_BOARD_FACTS_H
#define DOMAINS_GAMES_LIBS_CHESS_CPP_BOARD_FACTS_H

#include "chess.hpp"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp::facts {

/// Questions the motif detectors ask of a position.
///
/// This is the replacement for BoardUtils — the Java side's second,
/// hand-rolled implementation of how pieces move, kept in step with the
/// replayer's by nothing but care. Everything here either forwards to the
/// chess library's own move generation or is built out of it, so there is
/// one set of movement rules in the process.
///
/// The rule for what earns a place here: it is missing upstream, private
/// upstream, takes a Side where the library takes a chess::Color, or is
/// worth naming so detectors read as chess rather than as bit twiddling. Things the library already
/// answers well — inCheck(), isGameOver(), at(), kingSq() — are not wrapped, because a wrapper that
/// only renames is a second place to look.

/// The pieces giving check to `side`'s king, by square. Empty when `side`
/// is not in check, one square for a normal check, two for a double check.
///
/// The library computes this internally for move generation but exposes no
/// checkers() — this composes it from attackers() and kingSq(), which is
/// the same question asked out loud.
chess::Bitboard Checkers(const chess::Board& board, Side side);

/// True when two different pieces give check at once, so no capture and no
/// block can answer it and only the king may move. The one motif that is
/// purely a property of the position.
bool InDoubleCheck(const chess::Board& board, Side side);

/// Every piece of `by` that attacks `square` under the current occupancy.
///
/// Occupancy matters and is the whole subtlety: a rook behind another rook
/// is not in this set. Detectors that want the x-ray have to ask again with
/// the blocker removed.
chess::Bitboard AttackersOf(const chess::Board& board, Side by, chess::Square square);

/// The squares strictly between `from` and `to` along a shared rank, file,
/// or diagonal; empty when they are not aligned or are adjacent.
///
/// chess-library has this as movegen::between, but it is private with
/// Board as its only friend, so pin, skewer, and block logic cannot call
/// it. Rebuilt here from the public attack generators.
chess::Bitboard Between(chess::Square from, chess::Square to);

/// True when the two squares share a rank, file, or diagonal.
bool Aligned(chess::Square a, chess::Square b);

/// The squares the side to move's king can legally move to.
///
/// Legal, not pseudo-legal: squares that would leave the king in check are
/// already gone.
///
/// This is *not* a mate test, and the tempting reading is wrong in both
/// directions: a king with nowhere to go is not mated if the check can be
/// blocked or the checker captured, and one with nowhere to go is not
/// stalemated if any other piece can move. Ask isGameOver(), which answers
/// that question directly. What this is for is the mates that care *why*
/// the king cannot move — a smothered mate is "mate, and every escape is
/// blocked by the king's own men", and this is the second half.
chess::Bitboard SideToMoveKingEscapes(const chess::Board& board);

/// How a move gives check, if it does.
enum class CheckKind {
  kNone,
  /// The piece that moved is the one giving check.
  kDirect,
  /// The move uncovered a different piece's line to the king.
  kDiscovered,
};

/// Classifies the check `move` would give, from the position before it is
/// played.
///
/// This is the distinction the Java pipeline cannot draw. It infers
/// "discovered" from whether the moved piece landed on a checking ray,
/// which is a heuristic; this asks the move generator, which knows.
/// DISCOVERED_CHECK and the promotion-with-check split rest on it.
///
/// DOUBLE_CHECK does not: a move is one kind here, and a double check is
/// direct *and* discovered at once. Upstream returns on the first of the
/// two it tests, so a knight that moves to check while uncovering a rook
/// reports kDirect. Count Checkers() on the position after the move.
///
/// Castling is the one case this does not take from upstream. There, a
/// rook that lands giving check is reported as a discovery — it moved, so
/// it discovered nothing — which would file every castles-with-check as a
/// DISCOVERED_CHECK. Classified here from who is actually giving check
/// after the move instead.
CheckKind ClassifyCheck(const chess::Board& board, chess::Move move);

}  // namespace chess_cpp::facts

#endif  // DOMAINS_GAMES_LIBS_CHESS_CPP_BOARD_FACTS_H
