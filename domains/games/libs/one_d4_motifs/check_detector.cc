// The check family: CHECK, and the two the read path has been deriving
// from ATTACK rows because nothing stored them.
//
// DISCOVERED_CHECK is where this stops being a heuristic. The derivation
// asks whether a discovered ATTACK row happens to target a king, which
// depends on the discovery scan having produced that row; this asks the
// move generator what kind of check the move gives.

#include <algorithm>
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

class CheckDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kCheck; }

  void OnPosition(const Position& position, Findings& out) override {
    const chess::Board& board = position.board;
    if (!board.inCheck()) return;

    const chess::Square king = board.kingSq(chess_cpp::ToColor(position.side_to_move));
    const chess::Bitboard checkers = chess_cpp::facts::Checkers(board, position.side_to_move);
    const bool mate = IsCheckmate(position);

    Finding finding;
    finding.description =
        absl::StrCat(mate ? "Checkmate at move " : "Check at move ", position.move_number);
    // One attacker named even in double check, matching the column. The
    // second checker is not lost: ATTACK rows carry both, and DOUBLE_CHECK
    // is derived from them.
    const std::optional<chess::Square> checker = FirstInScanOrder(checkers);
    if (checker.has_value()) finding.attacker = PieceNotation(board.at(*checker), *checker);
    finding.target = PieceNotation(board.at(king), king);
    finding.is_mate = mate;
    out.Add(position, std::move(finding));
  }
};

/// DISCOVERED_CHECK: a checking piece that is not one the move put down.
///
/// Not ClassifyCheck: upstream's givesCheck returns the first kind it
/// tests, so a knight that checks while uncovering a rook comes back
/// DIRECT_CHECK and the discovery is lost — which is every double check.
/// Asking which checkers the move did not place answers both cases with
/// one question, and takes the landed squares from LandedOn so castling
/// (two pieces down) is not a special case here either.
class DiscoveredCheckDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kDiscoveredCheck; }

  void OnPosition(const Position& position, Findings& out) override {
    const chess::Board& board = position.board;
    if (!board.inCheck()) return;

    const std::vector<chess::Square> landed = LandedOn(position);
    chess::Bitboard checkers = chess_cpp::facts::Checkers(board, position.side_to_move);
    chess::Bitboard uncovered;
    while (checkers) {
      const chess::Square square(checkers.pop());
      if (std::find(landed.begin(), landed.end(), square) == landed.end()) {
        uncovered.set(square.index());
      }
    }
    const std::optional<chess::Square> checker = FirstInScanOrder(uncovered);
    if (!checker.has_value()) return;

    const chess::Square king = board.kingSq(chess_cpp::ToColor(position.side_to_move));
    const chess::Move move = position.last->move;
    Finding finding;
    finding.description = absl::StrCat("Discovered check at move ", position.move_number);
    finding.moved_piece =
        MovedPieceNotation(position.last->before.at(move.from()), move.from(),
                           landed.empty() ? std::nullopt : std::optional(landed.front()));
    finding.attacker = PieceNotation(board.at(*checker), *checker);
    finding.target = PieceNotation(board.at(king), king);
    finding.is_discovered = true;
    finding.is_mate = IsCheckmate(position);
    out.Add(position, std::move(finding));
  }
};

/// DOUBLE_CHECK: two pieces giving check at once, so only the king may
/// move. A property of the position, counted rather than inferred from how
/// many ATTACK rows happen to name the king.
class DoubleCheckDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kDoubleCheck; }

  void OnPosition(const Position& position, Findings& out) override {
    if (!chess_cpp::facts::InDoubleCheck(position.board, position.side_to_move)) return;

    const chess::Square king = position.board.kingSq(chess_cpp::ToColor(position.side_to_move));
    Finding finding;
    finding.description = absl::StrCat("Double check at move ", position.move_number);
    finding.target = PieceNotation(position.board.at(king), king);
    out.Add(position, std::move(finding));
  }
};

}  // namespace

std::unique_ptr<Detector> MakeCheckDetector() { return std::make_unique<CheckDetector>(); }

std::unique_ptr<Detector> MakeDiscoveredCheckDetector() {
  return std::make_unique<DiscoveredCheckDetector>();
}

std::unique_ptr<Detector> MakeDoubleCheckDetector() {
  return std::make_unique<DoubleCheckDetector>();
}

}  // namespace one_d4
