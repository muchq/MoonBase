#ifndef DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_H
#define DOMAINS_GAMES_LIBS_CHESS_PLAY_GAME_STATE_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/statusor.h"

namespace chess_play {

/// Chess at a games-hub table: two seats, a clock, and the full
/// rules — checkmate, stalemate, insufficient material, the fifty-move
/// rule and threefold repetition end the game on their own, unclaimed.
/// A variant is the material family a server-owned setup starts from.
///
/// The truth is the start position and the moves played from it, in UCI
/// ("e2e4", "e7e8q"): the position, repetition history and every ending
/// follow from replaying them. No chess-library type crosses this header.
///
/// The clock is Fischer: each side's remaining time, the moment the side
/// to move's turn began, and an increment added once a move is made. Time
/// is wall-clock epoch milliseconds handed in by the caller, so a test
/// never waits on one and every instance reads the same row the same way.
/// A side whose time is gone loses on time — or draws, when the other
/// side has nothing but its king to mate with.
enum class Color { kWhite, kBlack };

[[nodiscard]] std::string_view ColorName(Color color);

enum class Ending {
  kCheckmate,
  kStalemate,
  kInsufficientMaterial,
  kFiftyMoves,
  kRepetition,
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
  Ending ending;
  bool operator==(const Result&) const = default;
};

struct TimeControl {
  int64_t initial_ms = 0;
  int64_t increment_ms = 0;
  bool operator==(const TimeControl&) const = default;
};

/// Where a game starts, and which seat plays White.
struct Opening {
  std::string fen;
  int white_seat = 0;
};

inline constexpr std::string_view kStandardSetup = "standard";
inline constexpr std::string_view kDefaultChessSetup = kStandardSetup;
inline constexpr std::string_view kRandomKpkSetup = "random-kpk";

/// One server-owned starting-position choice presented to a client.
struct ChessSetup {
  std::string id;
  std::string name;
  std::string variant;
  Opening opening;
};

struct ChessSetupOption {
  std::string_view id;
  std::string_view name;
};

/// Selects a named setup, randomizing its position or White seat where
/// that setup calls for it.
[[nodiscard]] absl::StatusOr<ChessSetup> SelectChessSetup(std::string_view id,
                                                          absl::BitGenRef gen);
[[nodiscard]] std::vector<ChessSetupOption> AvailableChessSetups();
[[nodiscard]] std::optional<std::string_view> ChessSetupName(std::string_view id);
[[nodiscard]] std::optional<std::string_view> ChessSetupVariant(std::string_view id);

/// A random king-and-pawn-against-king position, White (the pawn's side)
/// to move: legal, not already over, the pawn on its second to sixth
/// rank. White is either seat.
[[nodiscard]] Opening RandomKpkOpening(absl::BitGenRef gen);

/// The clock as stored: each side's time left as of `turn_started_ms`,
/// which is when the side to move's turn began. Frozen once the game ends.
struct Clock {
  std::array<int64_t, 2> remaining_ms{};  // indexed by Color
  int64_t turn_started_ms = 0;
  bool operator==(const Clock&) const = default;
};

class GameState {
 public:
  static constexpr int kSeats = 2;
  static constexpr std::string_view kKpk = "kpk";
  /// Bounds a stored clock must keep for its arithmetic to hold: a day on
  /// any clock or increment, and a turn that started within the next
  /// thirty thousand years.
  static constexpr int64_t kMaxClockMs = 24LL * 60 * 60 * 1000;
  static constexpr int64_t kMaxEpochMs = 1'000'000'000'000'000;

  /// A game at `opening`, White's clock running from `now_ms`. Refuses
  /// anything but two distinct seats, a variant it does not know, a
  /// position that is not legal or is already over, and a time control
  /// with no time.
  [[nodiscard]] static absl::StatusOr<GameState> start(std::vector<std::string> players,
                                                       std::string variant, const Opening& opening,
                                                       TimeControl time_control, int64_t now_ms,
                                                       std::string setup_id =
                                                           std::string(kDefaultChessSetup));

  /// The full truth, validated as `start` validates plus every move legal
  /// in turn and the result, if any, one the moves could have produced —
  /// a timeout the one the stored clock shows. For a stored row.
  [[nodiscard]] static absl::StatusOr<GameState> restore(std::vector<std::string> players,
                                                         std::string variant, std::string start_fen,
                                                         int white_seat,
                                                         std::vector<std::string> moves,
                                                         TimeControl time_control, Clock clock,
                                                         std::optional<Result> result,
                                                         std::string setup_id =
                                                             std::string(kDefaultChessSetup));

  /// The seat on turn plays `uci`. A mover whose time ran out before it
  /// arrived does not move: the game ends on time instead. Off turn, a
  /// finished game, or a move not legal here is refused.
  [[nodiscard]] absl::StatusOr<GameState> move(int seat, std::string_view uci,
                                               int64_t now_ms) const;
  /// Either seat, on turn or not; after the flag fell, too late: the game
  /// ends on time instead.
  [[nodiscard]] absl::StatusOr<GameState> resign(int seat, int64_t now_ms) const;
  /// The game ended on time if the side to move's clock has run out at
  /// `now_ms`; FailedPrecondition if it has not.
  [[nodiscard]] absl::StatusOr<GameState> flag(int64_t now_ms) const;
  /// A seat left at `now_ms`: the other wins by abandonment, unless the
  /// side to move's time had already run out, which ended the game on time
  /// first. The clock stops where the leave found it. A finished game is
  /// returned unchanged.
  [[nodiscard]] absl::StatusOr<GameState> removePlayer(int seat, int64_t now_ms) const;

  [[nodiscard]] bool isOver() const { return result_.has_value(); }
  [[nodiscard]] const std::optional<Result>& result() const { return result_; }
  [[nodiscard]] int playerIndex(const std::string& id) const;
  [[nodiscard]] const std::vector<std::string>& players() const { return players_; }
  [[nodiscard]] const std::string& variant() const { return variant_; }
  [[nodiscard]] const std::string& setupId() const { return setup_id_; }
  [[nodiscard]] int whiteSeat() const { return white_seat_; }
  [[nodiscard]] int seatOf(Color color) const;
  [[nodiscard]] Color colorOf(int seat) const;
  [[nodiscard]] const std::string& startFen() const { return start_fen_; }
  [[nodiscard]] const std::vector<std::string>& moves() const { return moves_; }
  [[nodiscard]] const TimeControl& timeControl() const { return time_control_; }
  [[nodiscard]] const Clock& clock() const { return clock_; }

  /// The position now, as FEN.
  [[nodiscard]] const std::string& fen() const { return fen_; }
  [[nodiscard]] Color sideToMove() const { return side_to_move_; }
  /// The seat on turn; -1 once the game is over.
  [[nodiscard]] int whoseTurn() const;
  [[nodiscard]] bool inCheck() const { return in_check_; }
  /// Every legal move for the side to move, in UCI, sorted; empty once
  /// the game is over.
  [[nodiscard]] const std::vector<std::string>& legalMoves() const { return legal_moves_; }
  /// Time left for `color` at `now_ms`: the running side's clock counts
  /// down, never below zero; a finished game's clocks stand still.
  [[nodiscard]] int64_t remainingMs(Color color, int64_t now_ms) const;

 private:
  GameState(std::vector<std::string> players, std::string variant, std::string setup_id,
            std::string start_fen, int white_seat, std::vector<std::string> moves,
            TimeControl time_control, Clock clock, std::optional<Result> result);

  /// Replays the moves from the start, filling the derived position
  /// fields and the result the board itself reaches. Fails on an illegal
  /// move or an unreadable start.
  [[nodiscard]] absl::Status settle();
  [[nodiscard]] GameState ended(Result result, Clock clock) const;
  /// The clock with the side to move charged up to `now_ms` and its turn
  /// restarted there — never before it started, so a move stamped early
  /// (another instance's clock behind, or a wait for the hub's lock) costs
  /// the next side nothing.
  [[nodiscard]] Clock chargedTo(int64_t now_ms) const;
  [[nodiscard]] Result timeoutResult() const;

  std::vector<std::string> players_;
  std::string variant_;
  std::string setup_id_;
  std::string start_fen_;
  int white_seat_ = 0;
  std::vector<std::string> moves_;
  TimeControl time_control_;
  Clock clock_;
  std::optional<Result> result_;

  // Derived by settle().
  /// The ending the position itself has reached, if any.
  std::optional<Result> board_ending_;
  std::string fen_;
  Color side_to_move_ = Color::kWhite;
  bool in_check_ = false;
  std::vector<std::string> legal_moves_;
  /// Whether each side has only its king left, by Color: a side with
  /// nothing to mate with cannot win on time.
  std::array<bool, 2> bare_king_{};
};

}  // namespace chess_play

#endif
