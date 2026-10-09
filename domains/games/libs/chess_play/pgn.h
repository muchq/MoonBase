#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_PGN_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_PGN_H

#include <cstdint>
#include <string>
#include <vector>

#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {

/// A game read back move by move, for reviewing it: each move in SAN,
/// and every position as FEN from the start, so `fens[i]` is the board
/// after `i` moves and there is one more position than moves.
struct Replayed {
  std::vector<std::string> san;
  std::vector<std::string> fens;
};

/// `game`'s moves replayed from its start. A GameState only ever holds
/// moves that played, so this cannot fail.
[[nodiscard]] Replayed ReplayGame(const GameState& game);

/// The tags a game's PGN carries that the game itself does not know.
struct PgnTags {
  std::string event;
  std::string site;
  std::string round;
  /// When the game ended, epoch milliseconds: [Date], and [UTCDate] and
  /// [UTCTime], which is where an indexer reads the end time from.
  int64_t ended_at_ms = 0;
};

/// `game` as export-format PGN: the seven-tag roster, [UTCDate] and
/// [UTCTime], [SetUp]/[FEN] when
/// it did not start from the standard position, its [TimeControl] and,
/// once over, its [Termination]; then the moves in SAN, wrapped under
/// eighty columns, and the result ("*" while it is in play). Ends with a
/// newline, so games concatenate into an archive with a blank line
/// between them once the caller adds one.
[[nodiscard]] std::string ToPgn(const GameState& game, const PgnTags& tags);

}  // namespace chess_play

#endif  // DOMAINS_GAMES_LIBS_CHESS_PLAY_PGN_H
