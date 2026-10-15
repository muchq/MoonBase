#include "domains/games/libs/go/game_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace go {
namespace {

int Index(Color color) { return color == Color::kBlack ? 0 : 1; }

char StoneOf(Color color) { return color == Color::kBlack ? kBlackStone : kWhiteStone; }

absl::Status CheckSetup(const std::vector<std::string>& players, int size, double komi,
                        int black_seat, const TimeControl& tc) {
  if (players.size() != GameState::kSeats) {
    return absl::InvalidArgumentError("go needs exactly 2 players");
  }
  if (players[0] == players[1]) return absl::InvalidArgumentError("a player cannot play itself");
  if (size != 9 && size != 13 && size != 19) {
    return absl::InvalidArgumentError(absl::StrCat("a board is 9, 13 or 19, not ", size));
  }
  if (!std::isfinite(komi) || std::abs(komi) > kMaxKomi || komi * 2 != std::floor(komi * 2)) {
    return absl::InvalidArgumentError("komi is a multiple of 0.5 within 150");
  }
  if (black_seat != 0 && black_seat != 1) return absl::InvalidArgumentError("black seat is 0 or 1");
  if (tc.initial_ms <= 0 || tc.increment_ms < 0) {
    return absl::InvalidArgumentError("a clock needs time on it");
  }
  if (tc.initial_ms > GameState::kMaxClockMs || tc.increment_ms > GameState::kMaxClockMs) {
    return absl::InvalidArgumentError("a clock of more than a day");
  }
  return absl::OkStatus();
}

// The points orthogonally next to `point` on a `size` board.
template <typename F>
void ForNeighbours(int point, int size, F&& f) {
  const int x = point % size;
  const int y = point / size;
  if (x > 0) f(point - 1);
  if (x + 1 < size) f(point + 1);
  if (y > 0) f(point - size);
  if (y + 1 < size) f(point + size);
}

// The group at `point` (its colour's connected stones), and whether it
// has a liberty.
bool GroupHasLiberty(const std::string& board, int size, int point, std::vector<int>& group) {
  const char stone = board[point];
  group.clear();
  std::vector<bool> seen(board.size(), false);
  std::vector<int> frontier{point};
  seen[point] = true;
  bool liberty = false;
  while (!frontier.empty()) {
    const int at = frontier.back();
    frontier.pop_back();
    group.push_back(at);
    ForNeighbours(at, size, [&](int next) {
      if (board[next] == kEmpty) liberty = true;
      if (board[next] == stone && !seen[next]) {
        seen[next] = true;
        frontier.push_back(next);
      }
    });
  }
  return liberty;
}

}  // namespace

std::string_view ColorName(Color color) { return color == Color::kBlack ? "black" : "white"; }

Color Other(Color color) { return color == Color::kBlack ? Color::kWhite : Color::kBlack; }

std::string_view EndingName(Ending ending) {
  switch (ending) {
    case Ending::kScore:
      return "score";
    case Ending::kResignation:
      return "resignation";
    case Ending::kTimeout:
      return "timeout";
    case Ending::kAbandoned:
      return "abandoned";
  }
  return "score";
}

std::optional<Ending> ParseEnding(std::string_view name) {
  for (const Ending ending :
       {Ending::kScore, Ending::kResignation, Ending::kTimeout, Ending::kAbandoned}) {
    if (EndingName(ending) == name) return ending;
  }
  return std::nullopt;
}

GameState::GameState(std::vector<std::string> players, int size, double komi, int black_seat,
                     TimeControl time_control, Clock clock)
    : players_(std::move(players)),
      size_(size),
      komi_(komi),
      black_seat_(black_seat),
      time_control_(time_control),
      clock_(clock),
      board_(static_cast<std::size_t>(size) * size, kEmpty),
      previous_board_(board_) {}

absl::StatusOr<GameState> GameState::start(std::vector<std::string> players, int size, double komi,
                                           int black_seat, TimeControl time_control,
                                           int64_t now_ms) {
  if (auto ok = CheckSetup(players, size, komi, black_seat, time_control); !ok.ok()) return ok;
  const Clock clock{{time_control.initial_ms, time_control.initial_ms}, now_ms};
  return GameState(std::move(players), size, komi, black_seat, time_control, clock);
}

absl::StatusOr<GameState> GameState::restore(std::vector<std::string> players, int size,
                                             double komi, int black_seat, std::vector<int> moves,
                                             TimeControl time_control, Clock clock,
                                             std::optional<Result> result) {
  if (auto ok = CheckSetup(players, size, komi, black_seat, time_control); !ok.ok()) return ok;
  for (const int64_t remaining : clock.remaining_ms) {
    if (remaining < 0) return absl::InvalidArgumentError("a clock out of range");
  }
  if (clock.turn_started_ms < 0) return absl::InvalidArgumentError("a clock out of range");
  GameState state(std::move(players), size, komi, black_seat, time_control, clock);
  for (std::size_t i = 0; i < moves.size(); ++i) {
    if (state.twoPasses()) {
      return absl::InvalidArgumentError(absl::StrCat("move ", i + 1, " after the game ended"));
    }
    auto next = state.placed(moves[i]);
    if (!next.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("move ", i + 1, " is illegal: ", next.status().message()));
    }
    state = *std::move(next);
  }
  // Two passes end the game on the score, and nothing else does.
  if (state.twoPasses() != (result.has_value() && result->ending == Ending::kScore)) {
    return absl::InvalidArgumentError("a score ends exactly the game two passes end");
  }
  if (result.has_value() && result->ending == Ending::kScore && result != state.scoreResult()) {
    return absl::InvalidArgumentError("the stored score is not what the board counts");
  }
  if (result.has_value() && result->ending == Ending::kTimeout &&
      (state.clock_.remaining_ms[Index(state.side_to_move_)] != 0 ||
       result->winner != Other(state.side_to_move_))) {
    return absl::InvalidArgumentError("the stored timeout is not what the clock shows");
  }
  if (result.has_value() && result->ending != Ending::kScore && !result->winner.has_value()) {
    return absl::InvalidArgumentError("only a score is drawn");
  }
  state.result_ = std::move(result);
  return state;
}

absl::StatusOr<GameState> GameState::placed(int move) const {
  GameState next = *this;
  next.previous_board_ = board_;
  next.moves_.push_back(move);
  next.side_to_move_ = Other(side_to_move_);
  if (move == kPass) return next;
  const int points = size_ * size_;
  if (move < 0 || move >= points) return absl::InvalidArgumentError("off the board");
  if (board_[move] != kEmpty) return absl::InvalidArgumentError("the point is occupied");
  const char own = StoneOf(side_to_move_);
  const char theirs = StoneOf(Other(side_to_move_));
  next.board_[move] = own;
  std::vector<int> group;
  int taken = 0;
  ForNeighbours(move, size_, [&](int neighbour) {
    if (next.board_[neighbour] != theirs) return;
    if (GroupHasLiberty(next.board_, size_, neighbour, group)) return;
    for (const int stone : group) next.board_[stone] = kEmpty;
    taken += static_cast<int>(group.size());
  });
  if (!GroupHasLiberty(next.board_, size_, move, group)) {
    return absl::InvalidArgumentError("suicide is not allowed");
  }
  if (next.board_ == previous_board_)
    return absl::InvalidArgumentError("ko: retake elsewhere first");
  next.captures_[Index(side_to_move_)] += taken;
  return next;
}

bool GameState::twoPasses() const {
  const std::size_t n = moves_.size();
  return n >= 2 && moves_[n - 1] == kPass && moves_[n - 2] == kPass;
}

Score GameState::score() const {
  Score score{0, komi_};
  std::vector<bool> seen(board_.size(), false);
  for (std::size_t start = 0; start < board_.size(); ++start) {
    if (board_[start] == kBlackStone) {
      ++score.black;
      continue;
    }
    if (board_[start] == kWhiteStone) {
      ++score.white;
      continue;
    }
    if (seen[start]) continue;
    // An empty region: its points count for the one colour it reaches.
    int region = 0;
    bool reaches_black = false;
    bool reaches_white = false;
    std::vector<int> frontier{static_cast<int>(start)};
    seen[start] = true;
    while (!frontier.empty()) {
      const int at = frontier.back();
      frontier.pop_back();
      ++region;
      ForNeighbours(at, size_, [&](int next) {
        if (board_[next] == kBlackStone) reaches_black = true;
        if (board_[next] == kWhiteStone) reaches_white = true;
        if (board_[next] == kEmpty && !seen[next]) {
          seen[next] = true;
          frontier.push_back(next);
        }
      });
    }
    if (reaches_black && !reaches_white) score.black += region;
    if (reaches_white && !reaches_black) score.white += region;
  }
  return score;
}

Result GameState::scoreResult() const {
  const Score counted = score();
  if (counted.black > counted.white) return Result{Color::kBlack, Ending::kScore};
  if (counted.white > counted.black) return Result{Color::kWhite, Ending::kScore};
  return Result{std::nullopt, Ending::kScore};
}

absl::StatusOr<GameState> GameState::moved(int seat, int move, int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (seat != whoseTurn()) return absl::FailedPreconditionError("not your turn");
  if (remainingMs(side_to_move_, now_ms) == 0) {
    return ended(Result{Other(side_to_move_), Ending::kTimeout}, chargedTo(now_ms));
  }
  auto next = placed(move);
  if (!next.ok()) return next.status();
  next->clock_ = chargedTo(now_ms);
  next->clock_.remaining_ms[Index(side_to_move_)] += time_control_.increment_ms;
  if (next->twoPasses()) next->result_ = next->scoreResult();
  return next;
}

absl::StatusOr<GameState> GameState::play(int seat, int x, int y, int64_t now_ms) const {
  if (x < 0 || y < 0 || x >= size_ || y >= size_)
    return absl::InvalidArgumentError("off the board");
  return moved(seat, y * size_ + x, now_ms);
}

absl::StatusOr<GameState> GameState::pass(int seat, int64_t now_ms) const {
  return moved(seat, kPass, now_ms);
}

absl::StatusOr<GameState> GameState::resign(int seat, int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (remainingMs(side_to_move_, now_ms) == 0) {
    return ended(Result{Other(side_to_move_), Ending::kTimeout}, chargedTo(now_ms));
  }
  return ended(Result{Other(colorOf(seat)), Ending::kResignation}, chargedTo(now_ms));
}

absl::StatusOr<GameState> GameState::flag(int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (remainingMs(side_to_move_, now_ms) > 0) return absl::FailedPreconditionError("time remains");
  return ended(Result{Other(side_to_move_), Ending::kTimeout}, chargedTo(now_ms));
}

absl::StatusOr<GameState> GameState::removePlayer(int seat, int64_t now_ms) const {
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (isOver()) return *this;
  if (remainingMs(side_to_move_, now_ms) == 0) {
    return ended(Result{Other(side_to_move_), Ending::kTimeout}, chargedTo(now_ms));
  }
  return ended(Result{Other(colorOf(seat)), Ending::kAbandoned}, chargedTo(now_ms));
}

GameState GameState::ended(Result result, Clock clock) const {
  GameState next = *this;
  next.result_ = std::move(result);
  next.clock_ = clock;
  return next;
}

Clock GameState::chargedTo(int64_t now_ms) const {
  Clock clock = clock_;
  clock.remaining_ms[Index(side_to_move_)] = remainingMs(side_to_move_, now_ms);
  clock.turn_started_ms = std::max(now_ms, clock_.turn_started_ms);
  return clock;
}

int GameState::playerIndex(const std::string& id) const {
  for (int seat = 0; seat < kSeats; ++seat) {
    if (players_[seat] == id) return seat;
  }
  return -1;
}

int GameState::seatOf(Color color) const {
  return color == Color::kBlack ? black_seat_ : 1 - black_seat_;
}

Color GameState::colorOf(int seat) const {
  return seat == black_seat_ ? Color::kBlack : Color::kWhite;
}

int GameState::whoseTurn() const { return isOver() ? -1 : seatOf(side_to_move_); }

int GameState::captures(Color color) const { return captures_[Index(color)]; }

int64_t GameState::remainingMs(Color color, int64_t now_ms) const {
  const int64_t stored = clock_.remaining_ms[Index(color)];
  if (isOver() || color != side_to_move_) return stored;
  const int64_t elapsed = std::max<int64_t>(0, now_ms - clock_.turn_started_ms);
  return std::max<int64_t>(0, stored - elapsed);
}

}  // namespace go
