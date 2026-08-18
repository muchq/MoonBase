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

/// Squares the mover vacated, each with where that piece went. Castling
/// vacates two; a promotion has no destination in moved_piece's spelling,
/// because the pawn that left is not the piece that arrived.
std::vector<std::pair<chess::Square, std::optional<chess::Square>>> Vacated(
    const Position& position) {
  const chess::Move move = position.last->move;
  if (move.typeOf() == chess::Move::CASTLING) {
    // Encoded king-takes-rook: from is the king, to is the rook it swaps with.
    const bool kingside = move.to() > move.from();
    const int rank = move.from().index() / 8;
    const chess::Square king_to = chess::Square(rank * 8 + (kingside ? 6 : 2));
    const chess::Square rook_to = chess::Square(rank * 8 + (kingside ? 5 : 3));
    return {{move.from(), king_to}, {move.to(), rook_to}};
  }
  if (move.typeOf() == chess::Move::PROMOTION) return {{move.from(), std::nullopt}};
  return {{move.from(), move.to()}};
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

    if (const std::optional<chess::Square> landed = MovedTo(position); landed.has_value()) {
      DirectAttacks(position, *landed, mover, mate, out);
    }

    for (const auto& [vacated, destination] : Vacated(position)) {
      const chess::Piece moved = position.last->before.at(vacated);
      RevealedBy(position, vacated, destination, mover,
                 MovedPieceNotation(moved, vacated, destination), mate, &out);
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
