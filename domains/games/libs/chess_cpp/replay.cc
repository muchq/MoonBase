#include "domains/games/libs/chess_cpp/replay.h"

#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp {
namespace {

/// "12. Nf3" / "12... Nf6" — how a person refers to the move that failed.
std::string Describe(int move_number, bool white_moved, std::string_view san) {
  return absl::StrCat(move_number, white_moved ? ". " : "... ", san);
}

}  // namespace

absl::Status Replay(absl::Span<const std::string> san_moves,
                    absl::FunctionRef<void(const Position&)> on_position) {
  return ReplayFrom(chess::constants::STARTPOS, san_moves, on_position);
}

absl::Status ReplayFrom(std::string_view start_fen, absl::Span<const std::string> san_moves,
                        absl::FunctionRef<void(const Position&)> on_position) {
  chess::Board board;
  if (!board.setFen(start_fen)) {
    return absl::InvalidArgumentError(absl::StrCat("not a position: ", start_fen));
  }

  // setFen checks syntax, not whether the position could occur, and its
  // king check is off by default. Both gaps are reachable now that
  // StartFen() feeds this straight from a [FEN] tag:
  //
  //  - No king, and the first facts::Checkers or board.kingSq() on the
  //    result trips an assert inside the library (or, with NDEBUG, reads
  //    an empty bitboard's lsb).
  //  - The side *not* to move already in check is a position no game can
  //    reach, and the next move can capture a king: the replay runs to
  //    completion and hands out positions with pieces missing.
  //
  // Both are bad input rather than bad games, and saying so here beats
  // failing somewhere in a detector three modules away.
  if (!board.pieces(chess::PieceType::KING, chess::Color::WHITE) ||
      !board.pieces(chess::PieceType::KING, chess::Color::BLACK)) {
    return absl::InvalidArgumentError(absl::StrCat("position is missing a king: ", start_fen));
  }
  const chess::Color waiting = ~board.sideToMove();
  if (chess::attacks::attackers(board, board.sideToMove(), board.kingSq(waiting))) {
    return absl::InvalidArgumentError(
        absl::StrCat("position has the side not to move in check: ", start_fen));
  }

  // The same game, replayed one move behind, so every callback can hand out
  // the position its move was played from without anyone copying a board.
  // Kept in step by replaying the *previous* move just before the current
  // one is played — an extra makeMove per ply, and no allocation.
  chess::Board trailing = board;
  std::optional<chess::Move> previous_move;

  on_position(Position{
      .ply = 0,
      .move_number = 0,
      .side_to_move = FromColor(board.sideToMove()),
      .board = board,
      .last = std::nullopt,
  });

  int ply = 0;
  for (const std::string& san : san_moves) {
    ++ply;

    // Taken from the board rather than counted, so a game that starts from
    // a [FEN] header at move 20 reports move 20 and not move 1.
    const int move_number = static_cast<int>(board.fullMoveNumber());
    const Side mover = FromColor(board.sideToMove());

    chess::Move move;
    try {
      move = chess::uci::parseSan(board, san);
    } catch (const std::exception& error) {
      // The library's own message names the SAN and the FEN but not where
      // in the game it happened, and "which move" is the first thing anyone
      // asks. Rebuilt here rather than appended so the whole line reads in
      // one order.
      return absl::InvalidArgumentError(
          absl::StrCat("failed at ", Describe(move_number, mover == Side::kWhite, san), " from ",
                       board.getFen(), ": ", error.what()));
    }
    if (move == chess::Move::NO_MOVE) {
      return absl::InvalidArgumentError(
          absl::StrCat("failed at ", Describe(move_number, mover == Side::kWhite, san), " from ",
                       board.getFen(), ": not a move"));
    }

    if (previous_move.has_value()) trailing.makeMove(*previous_move);
    board.makeMove(move);
    previous_move = move;

    on_position(Position{
        .ply = ply,
        .move_number = move_number,
        .side_to_move = FromColor(board.sideToMove()),
        .board = board,
        .last = PlayedMove{.move = move, .san = san, .by = mover, .before = trailing},
    });
  }

  return absl::OkStatus();
}

}  // namespace chess_cpp
