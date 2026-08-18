// PIN and CROSS_PIN. Both are built from the same pin scan; a cross-pin is
// one piece pinned twice.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

struct Pin {
  chess::Square attacker;
  chess::Square target;
  PinType type;
};

/// The pin on the ray from `anchor`: exactly one piece of `pinned_side`
/// between the anchor and an enemy slider. Type is the caller's to say.
std::optional<Pin> PinAlong(const chess::Board& board, chess::Square anchor, Direction direction,
                            Side pinned_side, PinType type) {
  int r = RowOf(anchor) + direction.dr;
  int c = ColOf(anchor) + direction.dc;
  std::optional<chess::Square> candidate;

  while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
    const chess::Square square = SquareAt(r, c);
    const chess::Piece piece = board.at(square);
    if (piece != chess::Piece::NONE) {
      if (BelongsTo(piece, pinned_side)) {
        if (candidate.has_value()) return std::nullopt;  // two of ours: nothing is pinned
        candidate = square;
      } else {
        if (candidate.has_value() && SlidesAlong(piece, direction)) {
          return Pin{square, *candidate, type};
        }
        return std::nullopt;
      }
    }
    r += direction.dr;
    c += direction.dc;
  }
  return std::nullopt;
}

/// Absolute pins, plus relative pins onto a rook or queen behind.
std::vector<Pin> DetectPins(const chess::Board& board, Side pinned_side) {
  std::vector<Pin> pins;

  const chess::Square king = board.kingSq(chess_cpp::ToColor(pinned_side));
  for (const Direction direction : kDirections) {
    const std::optional<Pin> pin =
        PinAlong(board, king, direction, pinned_side, PinType::kAbsolute);
    if (pin.has_value()) pins.push_back(*pin);
  }

  // Relative: an enemy slider, one of ours, then something of ours worth
  // more. Rook or queen behind only, and never the king — that is the
  // absolute case above.
  ForEachSquare([&](int row, int col) {
    const chess::Square from = SquareAt(row, col);
    const chess::Piece attacker = board.at(from);
    if (!BelongsTo(attacker, chess_cpp::Opponent(pinned_side))) return;

    for (const Direction direction : kDirections) {
      if (!SlidesAlong(attacker, direction)) continue;

      int r = row + direction.dr;
      int c = col + direction.dc;
      std::optional<chess::Square> pinned;
      while (r >= 0 && r <= 7 && c >= 0 && c <= 7) {
        const chess::Square square = SquareAt(r, c);
        const chess::Piece piece = board.at(square);
        if (piece != chess::Piece::NONE) {
          if (!BelongsTo(piece, pinned_side)) break;
          if (!pinned.has_value()) {
            pinned = square;
          } else {
            const int behind = Value(piece);
            if (behind > Value(board.at(*pinned)) && behind >= Value(chess::PieceType::ROOK) &&
                piece.type() != chess::PieceType::KING) {
              pins.push_back({from, *pinned, PinType::kRelative});
            }
            break;
          }
        }
        r += direction.dr;
        c += direction.dc;
      }
    }
  });

  // Same attacker and target from two rays is one pin.
  std::vector<Pin> deduplicated;
  for (const Pin& pin : pins) {
    bool seen = false;
    for (const Pin& kept : deduplicated) {
      if (kept.attacker == pin.attacker && kept.target == pin.target) seen = true;
    }
    if (!seen) deduplicated.push_back(pin);
  }
  return deduplicated;
}

class PinDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kPin; }

  void OnPosition(const Position& position, Findings& out) override {
    const std::optional<chess::Square> landed = MovedTo(position);
    if (!landed.has_value()) return;

    const chess::Board& board = position.board;
    for (const Pin& pin : DetectPins(board, position.side_to_move)) {
      // Only the pin this move created: the pinning piece is the one that
      // just landed. A standing pin is not news every ply it persists.
      if (pin.attacker != *landed) continue;
      Finding finding;
      finding.description = absl::StrCat("Pin detected at move ", position.move_number);
      finding.attacker = PieceNotation(board.at(pin.attacker), pin.attacker);
      finding.target = PieceNotation(board.at(pin.target), pin.target);
      finding.pin_type = pin.type;
      out.Add(position, std::move(finding));
    }
  }
};

class CrossPinDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kCrossPin; }

  void OnPosition(const Position& position, Findings& out) override {
    const std::optional<chess::Square> landed = MovedTo(position);
    if (!landed.has_value()) return;

    // One piece held on two lines at once — to the king down a file, say,
    // and to a rook down a diagonal. Two pins with the same target and
    // different attackers, which is the only shape it can take: two rays
    // from one anchor never meet again.
    //
    // The Java detector looks for exactly that impossible shape — the same
    // square found from two directions off the king — so it has never
    // fired. This finds the real thing, and CROSS_PIN rows appear where
    // there were none.
    const std::vector<Pin> pins = DetectPins(position.board, position.side_to_move);
    for (std::size_t i = 0; i < pins.size(); ++i) {
      for (std::size_t j = i + 1; j < pins.size(); ++j) {
        if (pins[i].target != pins[j].target) continue;
        if (pins[i].attacker == pins[j].attacker) continue;
        // Only when this move made one of the two halves.
        if (pins[i].attacker != *landed && pins[j].attacker != *landed) continue;

        const chess::Board& board = position.board;
        Finding finding;
        finding.description = absl::StrCat("Cross-pin detected at move ", position.move_number);
        finding.attacker = PieceNotation(board.at(*landed), *landed);
        finding.target = PieceNotation(board.at(pins[i].target), pins[i].target);
        out.Add(position, std::move(finding));
        return;
      }
    }
  }
};

}  // namespace

std::unique_ptr<Detector> MakePinDetector() { return std::make_unique<PinDetector>(); }

std::unique_ptr<Detector> MakeCrossPinDetector() { return std::make_unique<CrossPinDetector>(); }

}  // namespace one_d4
