// ATTACK: the primitive the read path derives FORK, CHECKMATE,
// DISCOVERED_ATTACK, DISCOVERED_CHECK and DOUBLE_CHECK from. Every row it
// writes is one attacker bearing on one target after one move.
//
// Two kinds per move: what the piece that moved now attacks, and what the
// move uncovered. The second is why occurrences carry moved_piece — for a
// discovery, the mover and the attacker are different pieces.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/board_facts.h"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/chess_cpp/side.h"
#include "domains/games/libs/one_d4_motifs/board_scan.h"
#include "domains/games/libs/one_d4_motifs/detector.h"
#include "domains/games/libs/one_d4_motifs/detectors.h"
#include "domains/games/libs/one_d4_motifs/notation.h"

namespace one_d4 {
namespace {

using chess_cpp::Position;
using chess_cpp::Side;

bool IsKing(const chess::Board& board, chess::Square square) {
  return board.at(square).type() == chess::PieceType::KING;
}

bool IsKingOrQueen(const chess::Board& board, chess::Square square) {
  const chess::PieceType type = board.at(square).type();
  return type == chess::PieceType::KING || type == chess::PieceType::QUEEN;
}

/// Which of `targets` are worth a row: royalty always, and everything above
/// a pawn once two such pieces are hit at once — the fork the read path
/// looks for.
std::vector<chess::Square> Significant(const chess::Board& board,
                                       const std::vector<chess::Square>& targets) {
  std::vector<chess::Square> kept;
  for (const chess::Square target : targets) {
    if (IsKingOrQueen(board, target)) kept.push_back(target);
  }

  int valuable = 0;
  for (const chess::Square target : targets) {
    if (Value(board.at(target)) >= Value(chess::PieceType::KNIGHT)) ++valuable;
  }
  if (valuable < 2) return kept;

  for (const chess::Square target : targets) {
    if (Value(board.at(target)) < Value(chess::PieceType::KNIGHT)) continue;
    if (std::find(kept.begin(), kept.end(), target) == kept.end()) kept.push_back(target);
  }
  return kept;
}

/// A square the move emptied, and what a ray walking through it needs to
/// know.
struct Emptied {
  chess::Square square;

  /// Where the piece that left it went, so a ray does not report the piece
  /// that just moved as one it uncovered. Absent when nothing landed —
  /// the pawn taken en passant went nowhere.
  std::optional<chess::Square> landed;

  /// How the move is spelled in moved_piece, for a discovery through this
  /// square. Not always the piece that stood here: an en passant capture
  /// empties the taken pawn's square, and the piece that moved is the pawn
  /// that took it.
  std::string moved_piece;
};

/// Every square the move emptied.
///
/// Three moves empty a square that is not simply `from`, and each one is a
/// discovery the naive reading misses: castling moves a rook as well as a
/// king, en passant takes a pawn that is on neither square the move names,
/// and a promotion puts a piece down that is not the one that left.
std::vector<Emptied> EmptiedBy(const Position& position) {
  const chess::Move move = position.last->move;
  const chess::Board& before = position.last->before;
  const auto spell = [&before](chess::Square from, std::optional<chess::Square> to) {
    return MovedPieceNotation(before.at(from), from, to);
  };

  if (move.typeOf() == chess::Move::CASTLING) {
    // Encoded king-takes-rook: from is the king, to is the rook it swaps with.
    const bool kingside = move.to() > move.from();
    const chess::Square king_to = chess::Square(move.from().index() / 8 * 8 + (kingside ? 6 : 2));
    const chess::Square rook_to = CastledRookTo(move.from(), kingside);
    return {{move.from(), king_to, spell(move.from(), king_to)},
            {move.to(), rook_to, spell(move.to(), rook_to)}};
  }

  if (move.typeOf() == chess::Move::ENPASSANT) {
    // The taken pawn is beside the capturing pawn's origin, not on either
    // square the move names, and the diagonals and file through it open up
    // with no piece of ours anywhere near them.
    const chess::Square taken = SquareAt(RowOf(move.from()), ColOf(move.to()));
    const std::string moved = spell(move.from(), move.to());
    return {{move.from(), move.to(), moved}, {taken, std::nullopt, moved}};
  }

  if (move.typeOf() == chess::Move::PROMOTION) {
    // `landed` is the promotion square, so the new piece standing there is
    // not read as one the pawn had been hiding. It is still spelled without
    // a destination — the pawn that left is not the queen that arrived, and
    // stored rows carry the "??" that says so.
    return {{move.from(), move.to(), spell(move.from(), std::nullopt)}};
  }

  return {{move.from(), move.to(), spell(move.from(), move.to())}};
}

/// Attacks a slider had blocked by whatever stood on `vacated`.
///
/// Walk back from the empty square to find the piece that was hiding, then
/// forward to find what it now bears on. The piece that just moved is not a
/// discovery, however the ray runs through it.
void RevealedBy(const Position& position, chess::Square vacated,
                std::optional<chess::Square> landed, Side mover, const std::string& moved_piece,
                bool mate, Findings* out) {
  const chess::Board& board = position.board;
  for (const Direction direction : kDirections) {
    const std::optional<chess::Square> behind =
        FirstPieceAlong(board, RowOf(vacated), ColOf(vacated), direction.Reversed());
    if (!behind.has_value()) continue;
    if (landed.has_value() && *behind == *landed) continue;

    const chess::Piece attacker = board.at(*behind);
    if (!BelongsTo(attacker, mover) || !SlidesAlong(attacker, direction)) continue;

    const std::optional<chess::Square> target =
        FirstPieceAlong(board, RowOf(vacated), ColOf(vacated), direction);
    if (!target.has_value() || BelongsTo(board.at(*target), mover)) continue;

    Finding finding;
    finding.description = absl::StrCat("Discovered attack at move ", position.move_number);
    finding.moved_piece = moved_piece;
    finding.attacker = PieceNotation(attacker, *behind);
    finding.target = PieceNotation(board.at(*target), *target);
    finding.is_discovered = true;
    finding.is_mate = mate && board.at(*target).type() == chess::PieceType::KING;
    out->Add(position, std::move(finding));
  }
}

class AttackDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kAttack; }

  void OnPosition(const Position& position, Findings& out) override {
    const Side mover = chess_cpp::Opponent(position.side_to_move);
    const bool mate = IsCheckmate(position);

    for (const chess::Square landed : LandedOn(position)) {
      DirectAttacks(position, landed, mover, mate, out);
    }

    for (const Emptied& emptied : EmptiedBy(position)) {
      RevealedBy(position, emptied.square, emptied.landed, mover, emptied.moved_piece, mate, &out);
    }
  }

 private:
  static void DirectAttacks(const Position& position, chess::Square landed, Side mover, bool mate,
                            Findings& out) {
    const chess::Board& board = position.board;
    const chess::Piece piece = board.at(landed);
    if (piece == chess::Piece::NONE) return;

    const chess::Bitboard attacked = chess_cpp::facts::AttacksFrom(board, landed);
    std::vector<chess::Square> targets;
    ForEachSquare([&](int row, int col) {
      const chess::Square square = SquareAt(row, col);
      if (!attacked.check(square.index())) return;
      if (!BelongsTo(board.at(square), chess_cpp::Opponent(mover))) return;
      targets.push_back(square);
    });

    const std::string attacker = PieceNotation(piece, landed);
    for (const chess::Square target : Significant(board, targets)) {
      Finding finding;
      finding.description = absl::StrCat("Attack at move ", position.move_number);
      finding.moved_piece = attacker;
      finding.attacker = attacker;
      finding.target = PieceNotation(board.at(target), target);
      finding.is_mate = mate && IsKing(board, target);
      out.Add(position, std::move(finding));
    }
  }
};

}  // namespace

std::unique_ptr<Detector> MakeAttackDetector() { return std::make_unique<AttackDetector>(); }

}  // namespace one_d4
