#ifndef DOMAINS_GAMES_LIBS_CHESS_CPP_REPLAY_H
#define DOMAINS_GAMES_LIBS_CHESS_CPP_REPLAY_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/side.h"

namespace chess_cpp {

/// The move that produced a position, and the position it was played from.
///
/// One optional instead of three fields that all mean "unless this is the
/// starting position": a detector asks once, and there is no spelling of
/// "who moved" that quietly answers Black when nobody did.
struct PlayedMove {
  chess::Move move;

  /// `move` as the PGN spelled it ("Nf3", "exd6", "O-O-O", "e8=Q+").
  /// Borrowed from the caller's move list.
  std::string_view san;

  /// Who played it.
  Side by;

  /// The position `move` was played *from*.
  ///
  /// Here because the questions worth asking about a move are asked of the
  /// position before it: facts::ClassifyCheck, and every detector that
  /// compares what a square attacked before and after. Without it each of
  /// them would copy the board and unmake the move — a heap allocation per
  /// ply, thirteen times over, on a design whose whole point is not paying
  /// per-ply costs. The replayer keeps one extra board a move behind
  /// instead, and lends it out the same way it lends `board`.
  const chess::Board& before;
};

/// One position in a replayed game, as seen by whatever is walking it.
///
/// A borrowed view, valid only for the duration of the callback: the boards
/// belong to the replayer and it moves on. Copy what you need — a
/// detector's finding is a handful of squares, not a position.
struct Position {
  /// Half-moves played. 0 is the starting position.
  int ply = 0;

  /// The PGN full-move number of `last->move`; 0 at the starting position.
  /// Both of White's 1.e4 and Black's 1...e5 report 1, which is what a
  /// motif occurrence records and what a game viewer jumps to.
  int move_number = 0;

  /// Who moves *from* this position.
  Side side_to_move = Side::kWhite;

  /// The position itself.
  const chess::Board& board;

  /// How the game got here. Absent only at the starting position.
  std::optional<PlayedMove> last;
};

/// Replays a game from `start_fen`, calling `on_position` once for the
/// starting position and once after every move.
///
/// Games that do not start from the standard position are real archive
/// content, not a curiosity: chess.com serves "odds" games — a queen traded
/// for a bishop before the first move, say — with [SetUp "1"] and the
/// starting position in a [FEN] header. tactics_corpus.pgn holds four, one
/// of which gives Black a second queen, and a replayer that assumes the
/// standard start reads its moves as illegal. The Java pipeline has no way
/// to express this and drops such games with "failed to replay", which
/// indexes them as having no motifs at all.
///
/// StartFen() in pgn.h reads those headers; this takes what it returns.
///
/// Returns InvalidArgument if `start_fen` is not a position a game could be
/// in: unparseable, missing a king, or with the side not to move already in
/// check. The library's own setFen checks only syntax, and the other two
/// produce a replay that runs happily while handing out nonsense.
absl::Status ReplayFrom(std::string_view start_fen, absl::Span<const std::string> san_moves,
                        absl::FunctionRef<void(const Position&)> on_position);

/// Replays a game from the standard starting position, calling
/// `on_position` once for the initial position and once after every move.
///
/// This is the shape the whole pipeline is built around: one board, mutated
/// forward, handed out by reference. The Java pipeline it replaces
/// materializes a FEN string per ply and every detector re-parses each one
/// into an 8x8 array, so an N-ply game costs N serializations and ~13N
/// parses before any chess is played. Here a detector reads the board that
/// is already there.
///
/// Returns InvalidArgument for a move that will not play — illegal,
/// ambiguous, or not SAN at all — naming the ply, the move, and the FEN it
/// was tried from. That message is the one that ends up in a log next to a
/// chess.com game id, so it carries the position rather than just "illegal
/// move".
///
/// `on_position` returns void, unlike ParseGames' sink, and that asymmetry
/// is deliberate: a detector accumulates, it does not fail, so there is no
/// status the replayer could act on. Cancellation belongs a level up, in
/// the ParseGames sink, where it costs one check per game instead of one
/// per ply — a single game is microseconds. The Java replayer does check a
/// per-move interrupt flag, but only for the interactive analyze endpoint;
/// GameReplayer says outright that "indexing never interrupts". Porting
/// that endpoint (#1389) is when to revisit this.
absl::Status Replay(absl::Span<const std::string> san_moves,
                    absl::FunctionRef<void(const Position&)> on_position);

}  // namespace chess_cpp

#endif  // DOMAINS_GAMES_LIBS_CHESS_CPP_REPLAY_H
