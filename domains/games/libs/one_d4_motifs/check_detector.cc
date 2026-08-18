// CHECK, and the checkmating one as the same row with is_mate set.

#include <memory>
#include <optional>

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

}  // namespace

std::unique_ptr<Detector> MakeCheckDetector() { return std::make_unique<CheckDetector>(); }

}  // namespace one_d4
