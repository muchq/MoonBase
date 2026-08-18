#include "domains/games/libs/chess_cpp/replay.h"

#include <algorithm>
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
  // Kings are counted in the text, before the library is handed it, and
  // that ordering is the whole point rather than a style choice: setFen
  // itself is not safe to call on a kingless position. It builds the
  // castling paths as it parses, which calls kingSq() for both colours
  // unconditionally, and kingSq() asserts on an empty king bitboard — so a
  // [FEN] tag with a king missing aborts the process in a debug or
  // sanitizer build, and reads an empty bitboard's lsb under NDEBUG. A
  // check after the call could never run.
  const std::string_view placement = start_fen.substr(0, start_fen.find(' '));
  const auto count = [placement](char king) {
    return std::count(placement.begin(), placement.end(), king);
  };
  if (count('K') != 1 || count('k') != 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("not a position, wants exactly one king per side: ", start_fen));
  }

  chess::Board board;
  if (!board.setFen(start_fen)) {
    return absl::InvalidArgumentError(absl::StrCat("not a position: ", start_fen));
  }

  // The remaining gap is legality rather than syntax, and StartFen() feeds
  // this straight from a [FEN] tag: a position with the side *not* to move
  // already in check is one no game can reach, and the next move can
  // capture a king. Left alone the replay runs happily to the end while
  // handing out positions with pieces missing.
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
