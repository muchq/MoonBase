// SKEWER: a slider attacks a valuable piece with something cheaper behind
// it — a pin the other way up.

#include <memory>
#include <optional>
#include <vector>

#include "absl/strings/str_cat.h"
#include "chess.hpp"
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

struct Skewer {
  chess::Square attacker;
  chess::Square front;
};

/// The front piece of a skewer along `direction`, if there is one.
std::optional<chess::Square> SkewerAlong(const chess::Board& board, chess::Square from,
                                         Direction direction, Side attacking_side) {
  int r = RowOf(from) + direction.dr;
  int c = ColOf(from) + direction.dc;
  std::optional<chess::Square> front;

  while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
    const chess::Square square = SquareAt(r, c);
    const chess::Piece piece = board.at(square);
    if (piece != chess::Piece::NONE) {
      if (BelongsTo(piece, attacking_side)) return std::nullopt;  // our own piece blocks
      if (!front.has_value()) {
        front = square;
      } else {
        // Behind must be cheaper than the front piece, and worth taking.
        const int behind = Value(piece);
        if (Value(board.at(*front)) > behind && behind >= Value(chess::PieceType::KNIGHT)) {
          return front;
        }
        return std::nullopt;
      }
    }
    r += direction.dr;
    c += direction.dc;
  }
  return std::nullopt;
}

class SkewerDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kSkewer; }

  void OnPosition(const Position& position, Findings& out) override {
    const std::optional<chess::Square> landed = MovedTo(position);
    if (!landed.has_value()) return;

    const chess::Board& board = position.board;
    const Side attacking_side = chess_cpp::Opponent(position.side_to_move);

    std::vector<Skewer> skewers;
    ForEachSquare([&](int row, int col) {
      const chess::Square from = SquareAt(row, col);
      const chess::Piece piece = board.at(from);
      if (!BelongsTo(piece, attacking_side)) return;
      for (const Direction direction : kDirections) {
        if (!SlidesAlong(piece, direction)) continue;
        const std::optional<chess::Square> front =
            SkewerAlong(board, from, direction, attacking_side);
        if (front.has_value()) skewers.push_back({from, *front});
      }
    });

    for (const Skewer& skewer : skewers) {
      // Only the skewer this move created.
      if (skewer.attacker != *landed) continue;
      Finding finding;
      finding.description = absl::StrCat("Skewer detected at move ", position.move_number);
      finding.attacker = PieceNotation(board.at(skewer.attacker), skewer.attacker);
      finding.target = PieceNotation(board.at(skewer.front), skewer.front);
      out.Add(position, std::move(finding));
    }
  }
};

}  // namespace

std::unique_ptr<Detector> MakeSkewerDetector() { return std::make_unique<SkewerDetector>(); }

}  // namespace one_d4
