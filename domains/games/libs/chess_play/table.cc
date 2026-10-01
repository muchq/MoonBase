#include "domains/games/libs/chess_play/table.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace chess_play {

GameScore ScoreOf(const GameState& finished) {
  const Result& result = *finished.result();
  GameScore score;
  score.ending = result.ending;
  if (result.winner.has_value()) score.winner = finished.players().at(finished.seatOf(*result.winner));
  return score;
}

absl::StatusOr<Table> Table::open(std::vector<std::string> players, std::string variant,
                                  const Opening& opening, TimeControl time_control,
                                  int64_t now_ms) {
  auto game =
      GameState::start(std::move(players), std::move(variant), opening, time_control, now_ms);
  if (!game.ok()) return game.status();
  return Table(*std::move(game), {}, false, false);
}

absl::StatusOr<Table> Table::restore(GameState game, std::vector<GameScore> score_sheet,
                                     bool closed, bool ended_by_close) {
  for (const GameScore& line : score_sheet) {
    if (line.winner.has_value() && game.playerIndex(*line.winner) < 0) {
      return absl::InvalidArgumentError(absl::StrCat("not at this table: ", *line.winner));
    }
  }
  if (game.isOver() && (score_sheet.empty() || !(score_sheet.back() == ScoreOf(game)))) {
    return absl::InvalidArgumentError("the sheet does not score the game");
  }
  if (closed && !game.isOver()) return absl::InvalidArgumentError("closed over a game in play");
  if (ended_by_close && !closed) return absl::InvalidArgumentError("ended by a close it is not");
  return Table(std::move(game), std::move(score_sheet), closed, ended_by_close);
}

Table Table::withGame(GameState after, bool closed) const {
  const bool ended = !game_.isOver() && after.isOver();
  std::vector<GameScore> sheet = score_sheet_;
  if (ended) sheet.push_back(ScoreOf(after));
  return Table(std::move(after), std::move(sheet), closed, closed && ended);
}

absl::StatusOr<Table> Table::inGame(
    const std::function<absl::StatusOr<GameState>(const GameState&)>& move) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (game_.isOver()) return absl::FailedPreconditionError("no game in play");
  auto after = move(game_);
  if (!after.ok()) return after.status();
  return withGame(*std::move(after), false);
}

absl::StatusOr<Table> Table::next(Opening opening, TimeControl time_control,
                                  int64_t now_ms) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (!game_.isOver()) return absl::FailedPreconditionError("a game is in play");
  opening.white_seat = 1 - game_.whiteSeat();
  auto game = GameState::start(game_.players(), game_.variant(), opening, time_control, now_ms);
  if (!game.ok()) return game.status();
  return Table(*std::move(game), score_sheet_, false, false);
}

absl::StatusOr<Table> Table::removePlayer(int seat, int64_t now_ms) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  auto after = game_.removePlayer(seat, now_ms);
  if (!after.ok()) return after.status();
  return withGame(*std::move(after), true);
}

}  // namespace chess_play
