#ifndef DOMAINS_GAMES_LIBS_GO_GAME_STATE_H
#define DOMAINS_GAMES_LIBS_GO_GAME_STATE_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace go {

/// Go at a games-hub table: two seats on a 9, 13 or 19 board, Black first.
///
///   - A stone takes the opponent's groups it leaves without a liberty.
///   - Suicide is illegal: a stone whose own group is left without a
///     liberty, after its captures, is refused.
///   - Simple ko: a move may not recreate the board as it stood before
///     the opponent's last move.
///   - Two passes in a row end the game on the score: Tromp-Taylor area
///     (stones, plus empty regions that reach only that colour), komi to
///     White. An even score is a draw.
///
/// The truth is the moves played from the empty board; the position,
/// captures and score follow from replaying them. The clock is chess's
/// Fischer clock: each side's remaining time, the moment the side to
/// move's turn began, and an increment once it moves or passes. Time is
/// wall-clock epoch milliseconds handed in by the caller. A side whose
/// time is gone loses on time.
enum class Color { kBlack, kWhite };

[[nodiscard]] std::string_view ColorName(Color color);
[[nodiscard]] Color Other(Color color);

enum class Ending {
  kScore,
  kResignation,
  kTimeout,
  /// A seat left mid-game; the one still seated wins.
  kAbandoned,
};

[[nodiscard]] std::string_view EndingName(Ending ending);
[[nodiscard]] std::optional<Ending> ParseEnding(std::string_view name);

struct Result {
  /// Absent for a draw.
  std::optional<Color> winner;
  Ending ending = Ending::kScore;
  bool operator==(const Result&) const = default;
};

/// An area count, komi included in White's.
struct Score {
  double black = 0;
  double white = 0;
  bool operator==(const Score&) const = default;
};

struct TimeControl {
  int64_t initial_ms = 0;
  int64_t increment_ms = 0;
  bool operator==(const TimeControl&) const = default;
};

/// The clock as stored: each side's time left as of `turn_started_ms`,
/// which is when the side to move's turn began. Frozen once the game ends.
struct Clock {
  std::array<int64_t, 2> remaining_ms{};  // indexed by Color
  int64_t turn_started_ms = 0;
  bool operator==(const Clock&) const = default;
};

/// A move: a point by index (y * size + x), or a pass.
inline constexpr int kPass = -1;

/// What a point holds, as GameState::board() spells it.
inline constexpr char kEmpty = '.';
inline constexpr char kBlackStone = 'b';
inline constexpr char kWhiteStone = 'w';

inline constexpr double kDefaultKomi = 6.5;
inline constexpr double kMaxKomi = 150;

class GameState {
 public:
  static constexpr int kSeats = 2;
  /// Bounds a stored clock must keep for its arithmetic to hold: a day on
  /// any clock or increment.
  static constexpr int64_t kMaxClockMs = 24LL * 60 * 60 * 1000;

  /// An empty `size` board, Black (`black_seat`) to move, its clock
  /// running from `now_ms`. Refuses anything but two distinct seats, a
  /// size other than 9, 13 or 19, komi that is not a multiple of 0.5
  /// within ±150, and a time control with no time.
  [[nodiscard]] static absl::StatusOr<GameState> start(std::vector<std::string> players, int size,
                                                       double komi, int black_seat,
                                                       TimeControl time_control, int64_t now_ms);

  /// The full truth, validated as `start` validates plus every move legal
  /// in turn and the result, if any, one the moves could have produced: a
  /// score only after two passes and as the board counts, a timeout only
  /// on the clock that shows it. For a stored row.
  [[nodiscard]] static absl::StatusOr<GameState> restore(std::vector<std::string> players, int size,
                                                         double komi, int black_seat,
                                                         std::vector<int> moves,
                                                         TimeControl time_control, Clock clock,
                                                         std::optional<Result> result);

  /// The seat on turn places a stone at (x, y). A mover whose time ran out
  /// before it arrived does not move: the game ends on time instead. Off
  /// turn, a finished game, off the board, an occupied point, a suicide or
  /// a ko recapture is refused.
  [[nodiscard]] absl::StatusOr<GameState> play(int seat, int x, int y, int64_t now_ms) const;
  /// The seat on turn passes; the second pass in a row ends the game on
  /// the score.
  [[nodiscard]] absl::StatusOr<GameState> pass(int seat, int64_t now_ms) const;
  /// Either seat, on turn or not; after the flag fell, too late: the game
  /// ends on time instead.
  [[nodiscard]] absl::StatusOr<GameState> resign(int seat, int64_t now_ms) const;
  /// The game ended on time if the side to move's clock has run out at
  /// `now_ms`; FailedPrecondition if it has not.
  [[nodiscard]] absl::StatusOr<GameState> flag(int64_t now_ms) const;
  /// A seat left at `now_ms`: the other wins by abandonment, unless the
  /// side to move's time had already run out, which ended the game on time
  /// first. A finished game is returned unchanged.
  [[nodiscard]] absl::StatusOr<GameState> removePlayer(int seat, int64_t now_ms) const;

  [[nodiscard]] bool isOver() const { return result_.has_value(); }
  [[nodiscard]] const std::optional<Result>& result() const { return result_; }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] const std::vector<std::string>& players() const { return players_; }
  [[nodiscard]] int size() const { return size_; }
  [[nodiscard]] double komi() const { return komi_; }
  [[nodiscard]] int blackSeat() const { return black_seat_; }
  [[nodiscard]] int seatOf(Color color) const;
  [[nodiscard]] Color colorOf(int seat) const;
  [[nodiscard]] const std::vector<int>& moves() const { return moves_; }
  [[nodiscard]] const TimeControl& timeControl() const { return time_control_; }
  [[nodiscard]] const Clock& clock() const { return clock_; }

  /// The board, row by row from the top, `size` points a row.
  [[nodiscard]] const std::string& board() const { return board_; }
  [[nodiscard]] char at(int x, int y) const { return board_[y * size_ + x]; }
  [[nodiscard]] Color sideToMove() const { return side_to_move_; }
  /// The seat on turn; -1 once the game is over.
  [[nodiscard]] int whoseTurn() const;
  /// Stones `color` has taken.
  [[nodiscard]] int captures(Color color) const;
  /// The board's area count now, komi to White.
  [[nodiscard]] Score score() const;
  /// Time left for `color` at `now_ms`: the running side's clock counts
  /// down, never below zero; a finished game's clocks stand still.
  [[nodiscard]] int64_t remainingMs(Color color, int64_t now_ms) const;

 private:
  GameState(std::vector<std::string> players, int size, double komi, int black_seat,
            TimeControl time_control, Clock clock);

  /// The position after one more move by the side to move, the moves and
  /// clock untouched; refused as `play` refuses.
  [[nodiscard]] absl::StatusOr<GameState> placed(int move) const;
  [[nodiscard]] GameState ended(Result result, Clock clock) const;
  [[nodiscard]] Clock chargedTo(int64_t now_ms) const;
  /// The move charged to the clock, then placed; two passes end it.
  [[nodiscard]] absl::StatusOr<GameState> moved(int seat, int move, int64_t now_ms) const;
  [[nodiscard]] Result scoreResult() const;
  [[nodiscard]] bool twoPasses() const;

  std::vector<std::string> players_;
  int size_ = 19;
  double komi_ = kDefaultKomi;
  int black_seat_ = 0;
  std::vector<int> moves_;
  TimeControl time_control_;
  Clock clock_;
  std::optional<Result> result_;

  // Derived from the moves.
  std::string board_;
  /// The board before the last move, for ko.
  std::string previous_board_;
  Color side_to_move_ = Color::kBlack;
  std::array<int, 2> captures_{};  // indexed by Color
};

}  // namespace go

#endif  // DOMAINS_GAMES_LIBS_GO_GAME_STATE_H
