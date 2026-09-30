#include "domains/games/libs/chess_play/game_state.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "chess.hpp"

namespace chess_play {
namespace {

int Index(Color color) { return color == Color::kWhite ? 0 : 1; }
Color Other(Color color) { return color == Color::kWhite ? Color::kBlack : Color::kWhite; }
Color FromLibrary(chess::Color color) {
  return color == chess::Color::WHITE ? Color::kWhite : Color::kBlack;
}
chess::Color ToLibrary(Color color) {
  return color == Color::kWhite ? chess::Color::WHITE : chess::Color::BLACK;
}

// Endings the board reaches by itself; the rest are the players' doing.
bool IsBoardEnding(Ending ending) {
  switch (ending) {
    case Ending::kCheckmate:
    case Ending::kStalemate:
    case Ending::kInsufficientMaterial:
    case Ending::kFiftyMoves:
    case Ending::kRepetition:
      return true;
    case Ending::kResignation:
    case Ending::kTimeout:
    case Ending::kAbandoned:
      return false;
  }
  return false;
}

// A position a game can be played from: parseable, one king a side, and
// the side not to move not in check. The library's own parse accepts the
// rest, and a king en prise would let the next move capture it. Into
// `board`: the library's Board has virtual members and a non-virtual
// destructor, so it does not ride a StatusOr.
absl::Status ReadPosition(const std::string& fen, chess::Board& board) {
  const std::string_view placement = std::string_view(fen).substr(0, fen.find(' '));
  const auto count = [placement](char piece) {
    return std::count(placement.begin(), placement.end(), piece);
  };
  if (count('K') != 1 || count('k') != 1) {
    return absl::InvalidArgumentError(absl::StrCat("not a position: ", fen));
  }
  if (!board.setFen(fen)) return absl::InvalidArgumentError(absl::StrCat("not a position: ", fen));
  const chess::Color waiting = ~board.sideToMove();
  if (board.isAttacked(board.kingSq(waiting), board.sideToMove())) {
    return absl::InvalidArgumentError(absl::StrCat("the side not to move is in check: ", fen));
  }
  return absl::OkStatus();
}

std::vector<std::string> LegalUci(const chess::Board& board) {
  chess::Movelist list;
  chess::movegen::legalmoves(list, board);
  std::vector<std::string> moves;
  moves.reserve(list.size());
  for (const chess::Move& move : list) moves.push_back(chess::uci::moveToUci(move));
  std::sort(moves.begin(), moves.end());
  return moves;
}

std::optional<Result> BoardEnding(const chess::Board& board) {
  const auto [reason, outcome] = board.isGameOver();
  switch (reason) {
    case chess::GameResultReason::CHECKMATE:
      return Result{Other(FromLibrary(board.sideToMove())), Ending::kCheckmate};
    case chess::GameResultReason::STALEMATE:
      return Result{std::nullopt, Ending::kStalemate};
    case chess::GameResultReason::INSUFFICIENT_MATERIAL:
      return Result{std::nullopt, Ending::kInsufficientMaterial};
    case chess::GameResultReason::FIFTY_MOVE_RULE:
      // At the hundredth half-move a mate still wins; the library says
      // which through the outcome it pairs with the reason.
      if (outcome == chess::GameResult::LOSE) {
        return Result{Other(FromLibrary(board.sideToMove())), Ending::kCheckmate};
      }
      return Result{std::nullopt, Ending::kFiftyMoves};
    case chess::GameResultReason::THREEFOLD_REPETITION:
      return Result{std::nullopt, Ending::kRepetition};
    case chess::GameResultReason::NONE:
      break;
  }
  return std::nullopt;
}

absl::Status CheckSeats(const std::vector<std::string>& players, const std::string& variant,
                        int white_seat, const TimeControl& tc) {
  if (players.size() != GameState::kSeats) {
    return absl::InvalidArgumentError("chess needs exactly 2 players");
  }
  if (players[0] == players[1]) return absl::InvalidArgumentError("a player cannot play itself");
  if (variant != GameState::kKpk) {
    return absl::InvalidArgumentError(absl::StrCat("unknown variant: ", variant));
  }
  if (white_seat != 0 && white_seat != 1) return absl::InvalidArgumentError("white seat is 0 or 1");
  if (tc.initial_ms <= 0 || tc.increment_ms < 0) {
    return absl::InvalidArgumentError("a clock needs time on it");
  }
  if (tc.initial_ms > GameState::kMaxClockMs || tc.increment_ms > GameState::kMaxClockMs) {
    return absl::InvalidArgumentError("a clock of more than a day");
  }
  return absl::OkStatus();
}

}  // namespace

std::string_view ColorName(Color color) { return color == Color::kWhite ? "white" : "black"; }

std::string_view EndingName(Ending ending) {
  switch (ending) {
    case Ending::kCheckmate:
      return "checkmate";
    case Ending::kStalemate:
      return "stalemate";
    case Ending::kInsufficientMaterial:
      return "insufficientMaterial";
    case Ending::kFiftyMoves:
      return "fiftyMoves";
    case Ending::kRepetition:
      return "repetition";
    case Ending::kResignation:
      return "resignation";
    case Ending::kTimeout:
      return "timeout";
    case Ending::kAbandoned:
      return "abandoned";
  }
  return "abandoned";
}

std::optional<Ending> ParseEnding(std::string_view name) {
  for (const Ending ending :
       {Ending::kCheckmate, Ending::kStalemate, Ending::kInsufficientMaterial, Ending::kFiftyMoves,
        Ending::kRepetition, Ending::kResignation, Ending::kTimeout, Ending::kAbandoned}) {
    if (EndingName(ending) == name) return ending;
  }
  return std::nullopt;
}

Opening RandomKpkOpening(absl::BitGenRef gen) {
  while (true) {
    const int pawn_file = absl::Uniform(gen, 0, 8);
    const int pawn_rank = absl::Uniform(gen, 1, 6);  // ranks 2..6, zero-based
    const int white_king = absl::Uniform(gen, 0, 64);
    const int black_king = absl::Uniform(gen, 0, 64);
    const int pawn = pawn_rank * 8 + pawn_file;
    if (white_king == pawn || black_king == pawn || white_king == black_king) continue;
    const auto file = [](int square) { return square % 8; };
    const auto rank = [](int square) { return square / 8; };
    if (std::abs(file(white_king) - file(black_king)) <= 1 &&
        std::abs(rank(white_king) - rank(black_king)) <= 1) {
      continue;
    }
    // Rank by rank from the eighth, the way FEN reads.
    std::string placement;
    for (int r = 7; r >= 0; --r) {
      int empty = 0;
      for (int f = 0; f < 8; ++f) {
        const int square = r * 8 + f;
        char piece = 0;
        if (square == white_king) piece = 'K';
        if (square == black_king) piece = 'k';
        if (square == pawn) piece = 'P';
        if (piece == 0) {
          ++empty;
          continue;
        }
        if (empty > 0) placement += static_cast<char>('0' + empty);
        empty = 0;
        placement += piece;
      }
      if (empty > 0) placement += static_cast<char>('0' + empty);
      if (r > 0) placement += '/';
    }
    const std::string fen = absl::StrCat(placement, " w - - 0 1");
    // Black in check from the pawn, or White with nothing to play.
    chess::Board board;
    if (!ReadPosition(fen, board).ok() || BoardEnding(board).has_value()) continue;
    return Opening{fen, absl::Uniform(gen, 0, 2)};
  }
}

GameState::GameState(std::vector<std::string> players, std::string variant, std::string start_fen,
                     int white_seat, std::vector<std::string> moves, TimeControl time_control,
                     Clock clock, std::optional<Result> result)
    : players_(std::move(players)),
      variant_(std::move(variant)),
      start_fen_(std::move(start_fen)),
      white_seat_(white_seat),
      moves_(std::move(moves)),
      time_control_(time_control),
      clock_(clock),
      result_(std::move(result)) {}

absl::Status GameState::settle() {
  chess::Board position;
  if (auto read = ReadPosition(start_fen_, position); !read.ok()) return read;
  chess::Board* board = &position;
  for (std::size_t i = 0; i < moves_.size(); ++i) {
    if (BoardEnding(*board).has_value()) {
      return absl::InvalidArgumentError(absl::StrCat("move ", i + 1, " after the game ended"));
    }
    chess::Movelist list;
    chess::movegen::legalmoves(list, *board);
    const auto found = std::find_if(list.begin(), list.end(), [&](const chess::Move& move) {
      return chess::uci::moveToUci(move) == moves_[i];
    });
    if (found == list.end()) {
      return absl::InvalidArgumentError(absl::StrCat("move ", i + 1, " is illegal: ", moves_[i]));
    }
    board->makeMove(*found);
  }
  fen_ = board->getFen();
  side_to_move_ = FromLibrary(board->sideToMove());
  in_check_ = board->inCheck();
  board_ending_ = BoardEnding(*board);
  for (const Color color : {Color::kWhite, Color::kBlack}) {
    bare_king_[Index(color)] = board->us(ToLibrary(color)).count() == 1;
  }
  const bool over = board_ending_.has_value() || result_.has_value();
  legal_moves_ = over ? std::vector<std::string>{} : LegalUci(*board);
  return absl::OkStatus();
}

absl::StatusOr<GameState> GameState::start(std::vector<std::string> players, std::string variant,
                                           const Opening& opening, TimeControl time_control,
                                           int64_t now_ms) {
  if (auto seats = CheckSeats(players, variant, opening.white_seat, time_control); !seats.ok()) {
    return seats;
  }
  Clock clock;
  clock.remaining_ms = {time_control.initial_ms, time_control.initial_ms};
  clock.turn_started_ms = now_ms;
  GameState state(std::move(players), std::move(variant), opening.fen, opening.white_seat, {},
                  time_control, clock, std::nullopt);
  if (auto settled = state.settle(); !settled.ok()) return settled;
  if (state.board_ending_.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("the game is over already: ", opening.fen));
  }
  return state;
}

absl::StatusOr<GameState> GameState::restore(std::vector<std::string> players, std::string variant,
                                             std::string start_fen, int white_seat,
                                             std::vector<std::string> moves,
                                             TimeControl time_control, Clock clock,
                                             std::optional<Result> result) {
  if (auto seats = CheckSeats(players, variant, white_seat, time_control); !seats.ok()) {
    return seats;
  }
  for (const int64_t remaining : clock.remaining_ms) {
    if (remaining < 0 || remaining > kMaxClockMs) {
      return absl::InvalidArgumentError("a clock out of range");
    }
  }
  if (clock.turn_started_ms < 0 || clock.turn_started_ms > kMaxEpochMs) {
    return absl::InvalidArgumentError("a turn that started out of range");
  }
  GameState state(std::move(players), std::move(variant), std::move(start_fen), white_seat,
                  std::move(moves), time_control, clock, std::move(result));
  if (auto settled = state.settle(); !settled.ok()) return settled;
  // The board's ending is the only one it can have; without one, only
  // the players can have ended it.
  if (state.board_ending_.has_value()
          ? state.result_ != state.board_ending_
          : state.result_.has_value() && IsBoardEnding(state.result_->ending)) {
    return absl::InvalidArgumentError("the stored result is not what the moves reached");
  }
  // A timeout is the one the clock shows: the side to move out of time,
  // and the result a flag then gives.
  if (state.result_.has_value() && state.result_->ending == Ending::kTimeout &&
      (state.clock_.remaining_ms[Index(state.side_to_move_)] != 0 ||
       state.result_ != state.timeoutResult())) {
    return absl::InvalidArgumentError("the stored timeout is not what the clock shows");
  }
  return state;
}

absl::StatusOr<GameState> GameState::move(int seat, std::string_view uci, int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (seat != whoseTurn()) return absl::FailedPreconditionError("not your turn");
  if (remainingMs(side_to_move_, now_ms) == 0) return ended(timeoutResult(), chargedTo(now_ms));
  if (!std::binary_search(legal_moves_.begin(), legal_moves_.end(), uci)) {
    return absl::InvalidArgumentError(absl::StrCat("not a legal move: ", uci));
  }
  Clock clock = chargedTo(now_ms);
  clock.remaining_ms[Index(side_to_move_)] += time_control_.increment_ms;
  std::vector<std::string> moves = moves_;
  moves.emplace_back(uci);
  GameState next(players_, variant_, start_fen_, white_seat_, std::move(moves), time_control_,
                 clock, std::nullopt);
  if (auto settled = next.settle(); !settled.ok()) return settled;
  if (next.board_ending_.has_value()) {
    next.result_ = next.board_ending_;
    next.legal_moves_.clear();
  }
  return next;
}

absl::StatusOr<GameState> GameState::resign(int seat, int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (remainingMs(side_to_move_, now_ms) == 0) return ended(timeoutResult(), chargedTo(now_ms));
  return ended(Result{Other(colorOf(seat)), Ending::kResignation}, chargedTo(now_ms));
}

absl::StatusOr<GameState> GameState::flag(int64_t now_ms) const {
  if (isOver()) return absl::FailedPreconditionError("the game is over");
  if (remainingMs(side_to_move_, now_ms) > 0) {
    return absl::FailedPreconditionError("time remains");
  }
  return ended(timeoutResult(), chargedTo(now_ms));
}

absl::StatusOr<GameState> GameState::removePlayer(int seat, int64_t now_ms) const {
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (isOver()) return *this;
  if (remainingMs(side_to_move_, now_ms) == 0) return ended(timeoutResult(), chargedTo(now_ms));
  return ended(Result{Other(colorOf(seat)), Ending::kAbandoned}, chargedTo(now_ms));
}

GameState GameState::ended(Result result, Clock clock) const {
  GameState next = *this;
  next.result_ = std::move(result);
  next.clock_ = clock;
  next.legal_moves_.clear();
  return next;
}

Clock GameState::chargedTo(int64_t now_ms) const {
  Clock clock = clock_;
  clock.remaining_ms[Index(side_to_move_)] = remainingMs(side_to_move_, now_ms);
  clock.turn_started_ms = std::max(now_ms, clock_.turn_started_ms);
  return clock;
}

Result GameState::timeoutResult() const {
  const Color waiting = Other(side_to_move_);
  if (bare_king_[Index(waiting)]) return Result{std::nullopt, Ending::kTimeout};
  return Result{waiting, Ending::kTimeout};
}

int GameState::playerIndex(const std::string& id) const {
  for (int seat = 0; seat < kSeats; ++seat) {
    if (players_[seat] == id) return seat;
  }
  return -1;
}

int GameState::seatOf(Color color) const {
  return color == Color::kWhite ? white_seat_ : 1 - white_seat_;
}

Color GameState::colorOf(int seat) const {
  return seat == white_seat_ ? Color::kWhite : Color::kBlack;
}

int GameState::whoseTurn() const { return isOver() ? -1 : seatOf(side_to_move_); }

int64_t GameState::remainingMs(Color color, int64_t now_ms) const {
  const int64_t stored = clock_.remaining_ms[Index(color)];
  if (isOver() || color != side_to_move_) return stored;
  const int64_t elapsed = std::max<int64_t>(0, now_ms - clock_.turn_started_ms);
  return std::max<int64_t>(0, stored - elapsed);
}

}  // namespace chess_play
