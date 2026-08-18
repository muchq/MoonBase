// ZUGZWANG: declared since the schema existed, accepted by ChessQL,
// documented in CHESSQL.md, and never once emitted.
//
// The hard part is that zugzwang is defined by preference — the side to
// move would rather pass — and preference needs an evaluation this worker
// does not have. What it does have is a legal move generator, which is
// enough for the shape that matters: every move available loses material,
// and standing still would not.
//
// So the definition here is deliberately narrow, and named rather than
// approximated: not in check (that is not zugzwang, that is check), at
// least one legal move, nothing hanging as the position stands, and every
// legal move leaves something hanging. Positions that are zugzwang for
// positional reasons are missed. A row that fires is one where moving
// really does cost material.

#include <algorithm>
#include <memory>
#include <optional>
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
using chess_cpp::Side;

/// The most valuable piece of `side` the opponent can win here: attacked,
/// and either undefended or worth more than the cheapest piece attacking
/// it. Empty when nothing is hanging.
///
/// Walks `side`'s own pieces rather than all 64 squares, and the attacker
/// bitboards bit by bit. This runs once per legal move in the worst case,
/// and the worst case is a position where every move loses something.
std::optional<chess::Square> Hanging(const chess::Board& board, Side side) {
  const chess::Color color = chess_cpp::ToColor(side);
  std::optional<chess::Square> worst;
  int worst_value = 0;

  chess::Bitboard ours = board.us(color);
  while (ours) {
    const chess::Square square(ours.pop());
    const int value = Value(board.at(square));
    if (value <= worst_value) continue;

    chess::Bitboard attackers =
        chess_cpp::facts::AttackersOf(board, chess_cpp::Opponent(side), square);
    if (!attackers) continue;

    int cheapest = 7;
    while (attackers) {
      cheapest = std::min(cheapest, Value(board.at(chess::Square(attackers.pop()))));
    }

    const bool defended = static_cast<bool>(chess_cpp::facts::AttackersOf(board, side, square));
    if (!defended || value > cheapest) {
      worst = square;
      worst_value = value;
    }
  }
  return worst;
}

class ZugzwangDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kZugzwang; }

  // Attributed to the side that moved, like every other motif here: the
  // player who put the opponent in zugzwang. `target` names whose king is
  // stuck.
  void OnPosition(const Position& position, Findings& out) override {
    const chess::Board& board = position.board;
    const Side to_move = position.side_to_move;
    if (board.inCheck()) return;  // being in check is not being in zugzwang

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    if (moves.empty()) return;  // stalemate

    // Standing still would cost nothing.
    if (Hanging(board, to_move).has_value()) return;

    // ...but every move does, net of what it takes on the way. Hanging()
    // only ever sees the position after the move, so a capture's proceeds
    // are invisible to it: without this, taking a free bishop counts as a
    // loss because the pawn it stops defending is now en prise.
    chess::Board after = board;
    for (const chess::Move& move : moves) {
      const int captured = move.typeOf() == chess::Move::ENPASSANT ? Value(chess::PieceType::PAWN)
                                                                   : Value(board.at(move.to()));
      after.makeMove(move);
      const std::optional<chess::Square> hanging = Hanging(after, to_move);
      const int cost = hanging.has_value() ? Value(after.at(*hanging)) : 0;
      after.unmakeMove(move);
      if (captured >= cost) return;
    }

    Finding finding;
    finding.description = absl::StrCat("Zugzwang at move ", position.move_number);
    const chess::Square king = board.kingSq(chess_cpp::ToColor(to_move));
    finding.target = PieceNotation(board.at(king), king);
    out.Add(position, std::move(finding));
  }
};

}  // namespace

std::unique_ptr<Detector> MakeZugzwangDetector() { return std::make_unique<ZugzwangDetector>(); }

}  // namespace one_d4
