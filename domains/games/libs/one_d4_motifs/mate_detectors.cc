// BACK_RANK_MATE and SMOTHERED_MATE: both are checkmate plus a reason the
// king had nowhere to go. Only the final position can be mate, so both run
// there and nowhere else.

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

/// The piece named as delivering the mate.
std::optional<chess::Square> Checker(const chess::Board& board, Side mated) {
  return FirstInScanOrder(chess_cpp::facts::Checkers(board, mated));
}

/// CHECKMATE, as a row of its own.
///
/// The read path has been deriving this from ATTACK rows carrying is_mate,
/// which works for the WHERE clause and leaves ORDER BY and sequence()
/// matching nothing, because no detector has ever stored one. Mate is a
/// property of the position, so it is read from the position.
class CheckmateDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kCheckmate; }

  void OnGameEnd(const Position& final_position, Findings& out) override {
    if (!IsCheckmate(final_position)) return;

    const chess::Board& board = final_position.board;
    const Side mated = final_position.side_to_move;
    const chess::Square king = board.kingSq(chess_cpp::ToColor(mated));

    Finding finding;
    finding.description = absl::StrCat("Checkmate at move ", final_position.move_number);
    if (const std::optional<chess::Square> checker = Checker(board, mated); checker.has_value()) {
      finding.attacker = PieceNotation(board.at(*checker), *checker);
    }
    // The piece that moved, which on a discovered mate is not the piece
    // giving it.
    const chess::Move move = final_position.last->move;
    const std::vector<chess::Square> landed = LandedOn(final_position);
    finding.moved_piece =
        MovedPieceNotation(final_position.last->before.at(move.from()), move.from(),
                           landed.empty() ? std::nullopt : std::optional(landed.front()));
    finding.target = PieceNotation(board.at(king), king);
    finding.is_mate = true;
    out.Add(final_position, std::move(finding));
  }
};

class BackRankMateDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kBackRankMate; }

  void OnGameEnd(const Position& final_position, Findings& out) override {
    if (!IsCheckmate(final_position)) return;

    const chess::Board& board = final_position.board;
    const Side mated = final_position.side_to_move;
    const chess::Square king = board.kingSq(chess_cpp::ToColor(mated));

    const int back_rank = mated == Side::kWhite ? 0 : 7;
    if (king.index() / 8 != back_rank) return;

    // Mated *along* the back rank. Without this the motif fires on any
    // mate that happens to land on the back rank with a friendly pawn
    // nearby — a queen or bishop delivering mate from g7 is neither, and
    // the corpus is full of them.
    const std::optional<chess::Square> checker = Checker(board, mated);
    if (!checker.has_value() || checker->index() / 8 != back_rank) return;

    // ...and shut in by its own men rather than only by the attacker.
    const int escape_rank = mated == Side::kWhite ? 1 : 6;
    bool blocked = false;
    for (int file = ColOf(king) - 1; file <= ColOf(king) + 1; ++file) {
      if (file < 0 || file > 7) continue;
      if (BelongsTo(board.at(chess::Square(escape_rank * 8 + file)), mated)) blocked = true;
    }
    if (!blocked) return;

    Finding finding;
    finding.description = absl::StrCat("Back rank mate at move ", final_position.move_number);
    finding.attacker = PieceNotation(board.at(*checker), *checker);
    finding.target = PieceNotation(board.at(king), king);
    finding.is_mate = true;
    out.Add(final_position, std::move(finding));
  }
};

class SmotheredMateDetector : public Detector {
 public:
  Motif motif() const override { return Motif::kSmotheredMate; }

  void OnGameEnd(const Position& final_position, Findings& out) override {
    if (!IsCheckmate(final_position)) return;

    const chess::Board& board = final_position.board;
    const Side mated = final_position.side_to_move;
    const chess::Square king = board.kingSq(chess_cpp::ToColor(mated));

    // Mated by a knight...
    std::optional<chess::Square> knight;
    ForEachSquare([&](int row, int col) {
      if (knight.has_value()) return;
      const chess::Square square = SquareAt(row, col);
      const chess::Piece piece = board.at(square);
      if (piece.type() != chess::PieceType::KNIGHT) return;
      if (BelongsTo(piece, mated)) return;
      if (chess_cpp::facts::AttacksFrom(board, square).check(king.index())) knight = square;
    });
    if (!knight.has_value()) return;

    // ...with every square around the king filled by its own side.
    for (const Direction direction : kDirections) {
      const std::optional<chess::Square> neighbour = Step(RowOf(king), ColOf(king), direction);
      if (!neighbour.has_value()) continue;  // the edge blocks as well as a pawn does
      if (!BelongsTo(board.at(*neighbour), mated)) return;
    }

    Finding finding;
    finding.description = absl::StrCat("Smothered mate at move ", final_position.move_number);
    finding.attacker = PieceNotation(board.at(*knight), *knight);
    finding.target = PieceNotation(board.at(king), king);
    finding.is_mate = true;
    out.Add(final_position, std::move(finding));
  }
};

}  // namespace

std::unique_ptr<Detector> MakeCheckmateDetector() { return std::make_unique<CheckmateDetector>(); }

std::unique_ptr<Detector> MakeBackRankMateDetector() {
  return std::make_unique<BackRankMateDetector>();
}

std::unique_ptr<Detector> MakeSmotheredMateDetector() {
  return std::make_unique<SmotheredMateDetector>();
}

}  // namespace one_d4
