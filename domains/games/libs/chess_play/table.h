#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_TABLE_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_TABLE_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/chess_play/game_state.h"

namespace chess_play {

/// A line of the table's score sheet: one game's end. The winner by
/// name, since sides swap every game, and the color they won with; both
/// absent for a draw.
struct GameScore {
  std::optional<std::string> winner;
  Ending ending = Ending::kCheckmate;
  std::optional<Color> winner_color;
  bool operator==(const GameScore&) const = default;
};

/// A chess table: two seats playing one game after another until one of
/// them leaves, the way a rummy table deals on.
///
///   - It opens on its first game. A game's end is a line on the score
///     sheet, and the table stays open for the next.
///   - The next game swaps sides: whoever played Black plays White, so in
///     king and pawn against king the pawn goes back and forth.
///   - A leave closes the table. In the middle of a game it ends that
///     game first — lost by abandonment, or on time if the flag had
///     already fallen — and that game is scored; between games it scores
///     nothing.
class Table {
 public:
  /// A table for `players` on its first game.
  [[nodiscard]] static absl::StatusOr<Table> open(std::vector<std::string> players,
                                                  std::string variant, const Opening& opening,
                                                  TimeControl time_control, int64_t now_ms);
  /// The full truth, for a stored row: refused unless the sheet ends with
  /// the game's result exactly when the game is over, names only the
  /// table's players, and a closed table's game is over.
  [[nodiscard]] static absl::StatusOr<Table> restore(GameState game,
                                                     std::vector<GameScore> score_sheet,
                                                     bool closed, bool ended_by_close);

  /// A move in the game in play; a game it ends is scored. Refused
  /// between games and once the table is closed.
  [[nodiscard]] absl::StatusOr<Table> inGame(
      const std::function<absl::StatusOr<GameState>(const GameState&)>& move) const;
  /// The next game, sides swapped whatever `opening` says. Only between
  /// games.
  [[nodiscard]] absl::StatusOr<Table> next(Opening opening, TimeControl time_control,
                                           int64_t now_ms) const;
  /// A seat leaving at `now_ms`: the table closes.
  [[nodiscard]] absl::StatusOr<Table> removePlayer(int seat, int64_t now_ms) const;

  /// Closed: a seat left.
  [[nodiscard]] bool isOver() const { return closed_; }
  /// The close ended the game in play, which the sheet's last line scores.
  [[nodiscard]] bool endedByClose() const { return ended_by_close_; }
  /// The game in play, or the one that just ended.
  [[nodiscard]] const GameState& game() const { return game_; }
  [[nodiscard]] const std::vector<GameScore>& scoreSheet() const { return score_sheet_; }
  [[nodiscard]] const std::vector<std::string>& players() const { return game_.players(); }
  [[nodiscard]] int playerIndex(const std::string& id) const { return game_.playerIndex(id); }

 private:
  Table(GameState game, std::vector<GameScore> score_sheet, bool closed, bool ended_by_close)
      : game_(std::move(game)),
        score_sheet_(std::move(score_sheet)),
        closed_(closed),
        ended_by_close_(ended_by_close) {}
  // `after` in place of the game, scored if it ended it.
  [[nodiscard]] Table withGame(GameState after, bool closed) const;

  GameState game_;
  std::vector<GameScore> score_sheet_;
  bool closed_ = false;
  bool ended_by_close_ = false;
};

/// The line a finished game scores.
[[nodiscard]] GameScore ScoreOf(const GameState& finished);

}  // namespace chess_play

#endif  // DOMAINS_GAMES_LIBS_CHESS_PLAY_TABLE_H
