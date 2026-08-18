// PROMOTION, and the two that ask whether the new piece is the one giving
// check. A pawn promoting while a rook behind it gives check is a
// discovery, not a promotion with check — the notation cannot tell them
// apart, so ask the board.

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

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

/// The promoted piece itself attacks the enemy king.
bool PromotedPieceChecks(const Position& position) {
  if (!IsPromotion(position)) return false;
  const chess::Board& board = position.board;
  const chess::Square promoted = position.last->move.to();
  const chess::Square king = board.kingSq(chess_cpp::ToColor(position.side_to_move));
  return chess_cpp::facts::AttacksFrom(board, promoted).check(king.index());
}

Finding PromotionCheckFinding(const Position& position, std::string_view label, bool mate) {
  const chess::Board& board = position.board;
  const chess::Square promoted = position.last->move.to();
  const chess::Square king = board.kingSq(chess_cpp::ToColor(position.side_to_move));

  Finding finding;
  finding.description = absl::StrCat(label, position.move_number);
  finding.attacker = PieceNotation(board.at(promoted), promoted);
  finding.target = PieceNotation(board.at(king), king);
  finding.is_mate = mate;
  return finding;
}

class PromotionDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kPromotion; }

  void OnPosition(const Position& position, Findings& out) override {
    if (!IsPromotion(position)) return;
    Finding finding;
    finding.description = absl::StrCat("Promotion at move ", position.move_number);
    out.Add(position, std::move(finding));
  }
};

class PromotionWithCheckDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kPromotionWithCheck; }

  void OnPosition(const Position& position, Findings& out) override {
    if (!position.board.inCheck() || IsCheckmate(position)) return;
    if (!PromotedPieceChecks(position)) return;
    out.Add(position, PromotionCheckFinding(position, "Promotion with check at move ", false));
  }
};

class PromotionWithCheckmateDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kPromotionWithCheckmate; }

  void OnGameEnd(const Position& final_position, Findings& out) override {
    if (!IsCheckmate(final_position)) return;
    if (!PromotedPieceChecks(final_position)) return;
    out.Add(final_position,
            PromotionCheckFinding(final_position, "Promotion with checkmate at move ", true));
  }
};

}  // namespace

std::unique_ptr<Detector> MakePromotionDetector() { return std::make_unique<PromotionDetector>(); }

std::unique_ptr<Detector> MakePromotionWithCheckDetector() {
  return std::make_unique<PromotionWithCheckDetector>();
}

std::unique_ptr<Detector> MakePromotionWithCheckmateDetector() {
  return std::make_unique<PromotionWithCheckmateDetector>();
}

}  // namespace one_d4
