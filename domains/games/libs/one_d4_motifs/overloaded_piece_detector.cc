// OVERLOADED_PIECE: a defender doing two jobs it cannot both keep.
//
// Declared, queryable, and always empty until now — BoardUtils has no
// notion of a defender at all, so the Java pipeline had nothing to build it
// from. attackers(board, color, square) asked of both colors gives it
// directly: for any square, who attacks it and who defends it.
//
// A piece is overloaded when two or more of our pieces are attacked and it
// is the only thing defending each. Take one and the other falls.
// Deliberately not "defends two things", which is most pieces in most
// positions and would say nothing.

#include <memory>
#include <optional>
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

/// A piece, the attacked squares it alone defends, and everything of
/// theirs bearing on those squares.
struct Duties {
  chess::Square defender;
  std::vector<chess::Square> defended;
  chess::Bitboard attackers;
};

class OverloadedPieceDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kOverloadedPiece; }

  // Attributed to the side that just moved — the one with something to
  // win — while the overloaded piece itself belongs to the side to move.
  void OnPosition(const Position& position, Findings& out) override {
    const chess::Board& board = position.board;
    const Side defending = position.side_to_move;
    const Side attacking = chess_cpp::Opponent(defending);

    // For each of our pieces, the attacked squares it alone defends.
    std::vector<Duties> duties;
    ForEachSquare([&](int row, int col) {
      const chess::Square square = SquareAt(row, col);
      if (!BelongsTo(board.at(square), defending)) return;
      // A king in check is not a piece being held; without this every check
      // with a single defender on the board reads as an overload, and a
      // knight fork reads as one twice.
      if (board.at(square).type() == chess::PieceType::KING) return;
      // The defence has to be holding something back. A pawn attacked by a
      // queen is not defended in any meaningful sense — taking it loses the
      // queen — and counting those makes half the pieces on the board
      // overloaded.
      const std::optional<int> cheapest = CheapestAttackerValue(board, attacking, square);
      if (!cheapest.has_value() || *cheapest > Value(board.at(square))) return;

      const chess::Bitboard defenders = chess_cpp::facts::AttackersOf(board, defending, square);
      if (defenders.count() != 1) return;

      const chess::Square defender(defenders.lsb());
      const chess::Bitboard attackers = chess_cpp::facts::AttackersOf(board, attacking, square);
      for (Duties& duty : duties) {
        if (duty.defender == defender) {
          duty.defended.push_back(square);
          duty.attackers |= attackers;
          return;
        }
      }
      duties.push_back({defender, {square}, attackers});
    });

    for (const auto& [defender, defended, attackers] : duties) {
      if (defended.size() < 2) continue;
      // Two jobs and one attacker is not an overload: it can cash one
      // square, the defender recaptures, and the other square is no longer
      // attacked. Pawn tension reads as an overload without this.
      if (attackers.count() < 2) continue;
      // One row per piece it cannot keep, so the read path can name both
      // halves of the tactic.
      for (const chess::Square square : defended) {
        Finding finding;
        finding.description = absl::StrCat("Overloaded piece at move ", position.move_number);
        finding.attacker = PieceNotation(board.at(defender), defender);
        finding.target = PieceNotation(board.at(square), square);
        out.Add(position, std::move(finding));
      }
    }
  }
};

}  // namespace

std::unique_ptr<Detector> MakeOverloadedPieceDetector() {
  return std::make_unique<OverloadedPieceDetector>();
}

}  // namespace one_d4
