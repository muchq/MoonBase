#ifndef DOMAINS_GAMES_LIBS_CHESS_CPP_PGN_H
#define DOMAINS_GAMES_LIBS_CHESS_CPP_PGN_H

#include <istream>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"

namespace chess_cpp {

// Reads PGN text into games. No board, no legality: this layer answers
// "what did the file say", and Replay() answers "is that a game".
//
// What the underlying reader already handles, verified in
// chess_library_contract_test rather than assumed here: variations are
// skipped, NAGs ($1) are dropped, comments are delivered out of band, and
// the result token (1-0, 1/2-1/2, *) never arrives as a move. That last one
// is why there is no "does this look like SAN" filter anywhere in this
// library — the Java pipeline needs one because chariot hands NAGs back as
// move tokens, and that whole class of bug is absent here.
//
// Comments are dropped for now. chess.com writes a clock into every one
// ({[%clk 0:02:57]}), so this is where per-move time control would enter if
// the indexer ever wants it; the reader hands it to us already.

/// Parses exactly one game.
///
/// Returns InvalidArgument when the text holds no game or more than one —
/// both are caller bugs worth hearing about. Silently taking the first of
/// several is how an archive gets indexed as a single game.
absl::StatusOr<ParsedGame> ParseGame(std::string_view pgn);

/// Parses concatenated games from a stream, calling `on_game` for each in
/// file order.
///
/// The stream shape is the point: a chess.com month archive is many games,
/// and this never holds more than one of them at a time. If `on_game`
/// returns a non-OK status, no further games are delivered and that status
/// is what comes back.
absl::Status ParseGames(std::istream& stream, absl::FunctionRef<absl::Status(ParsedGame)> on_game);

/// Where the game began: the [FEN] tag when [SetUp "1"] says it did not
/// start from the standard position, and the standard position otherwise.
/// Feed the result to ReplayFrom().
///
/// Returns InvalidArgument when the two tags disagree — one without the
/// other is a malformed pair, and the two ways of guessing are both bad: a
/// [FEN] ignored because [SetUp] is missing replays an odds game from the
/// wrong position and calls its legal moves illegal, while a [SetUp] with
/// no [FEN] names no position to start from.
absl::StatusOr<std::string_view> StartFen(const Headers& headers);

}  // namespace chess_cpp

#endif  // DOMAINS_GAMES_LIBS_CHESS_CPP_PGN_H
