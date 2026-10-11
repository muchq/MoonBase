#include "domains/games/libs/pong/table.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace pong {
namespace {

absl::Status CheckPlayers(const std::vector<std::string>& players) {
  if (players.size() != kSeats) return absl::InvalidArgumentError("pong seats two");
  if (players[0].empty() || players[1].empty()) {
    return absl::InvalidArgumentError("a seat needs a player");
  }
  if (players[0] == players[1]) return absl::InvalidArgumentError("two different players");
  return absl::OkStatus();
}

}  // namespace

std::string_view VariantName(Variant variant) {
  return variant == Variant::kThreeD ? "3d" : "smallcat";
}

std::optional<Variant> ParseVariant(std::string_view name) {
  if (name == "smallcat") return Variant::kSmallcat;
  if (name == "3d") return Variant::kThreeD;
  return std::nullopt;
}

std::string_view EndingName(Ending ending) {
  return ending == Ending::kAbandoned ? "abandoned" : "points";
}

std::optional<Ending> ParseEnding(std::string_view name) {
  if (name == "points") return Ending::kPoints;
  if (name == "abandoned") return Ending::kAbandoned;
  return std::nullopt;
}

absl::StatusOr<Table> Table::open(std::vector<std::string> players, Variant variant) {
  if (const absl::Status ok = CheckPlayers(players); !ok.ok()) return ok;
  return Table(std::move(players), variant, {kLives, kLives}, 0, {}, true, false, false);
}

absl::StatusOr<Table> Table::restore(std::vector<std::string> players, Variant variant,
                                     std::array<int, kSeats> lives, int points,
                                     std::vector<GameScore> score_sheet, bool in_play, bool closed,
                                     bool ended_by_close) {
  if (const absl::Status ok = CheckPlayers(players); !ok.ok()) return ok;
  for (const int left : lives) {
    if (left < 0 || left > kLives) return absl::InvalidArgumentError("lives out of range");
  }
  if (points < 0) return absl::InvalidArgumentError("points out of range");
  const bool someone_out = lives[0] == 0 || lives[1] == 0;
  if (in_play && someone_out) return absl::InvalidArgumentError("in play with a seat out of lives");
  if (someone_out && score_sheet.empty()) {
    return absl::InvalidArgumentError("the sheet does not score the game");
  }
  for (const GameScore& line : score_sheet) {
    if (line.winner.has_value() &&
        std::find(players.begin(), players.end(), *line.winner) == players.end()) {
      return absl::InvalidArgumentError(absl::StrCat("not at this table: ", *line.winner));
    }
  }
  if (closed && in_play) return absl::InvalidArgumentError("closed over a game in play");
  if (ended_by_close && !closed) return absl::InvalidArgumentError("ended by a close it is not");
  return Table(std::move(players), variant, lives, points, std::move(score_sheet), in_play, closed,
               ended_by_close);
}

absl::StatusOr<Table> Table::point(int missed_by) const {
  if (missed_by < 0 || missed_by >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (!in_play_) return absl::FailedPreconditionError("no game in play");
  Table after = *this;
  ++after.points_;
  if (--after.lives_[missed_by] == 0) {
    after.in_play_ = false;
    after.score_sheet_.push_back({players_[1 - missed_by], Ending::kPoints});
  }
  return after;
}

absl::StatusOr<Table> Table::next(Variant variant) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (in_play_) return absl::FailedPreconditionError("a game is in play");
  return Table(players_, variant, {kLives, kLives}, 0, score_sheet_, true, false, false);
}

absl::StatusOr<Table> Table::removePlayer(int seat) const {
  if (seat < 0 || seat >= kSeats) return absl::InvalidArgumentError("no such seat");
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  Table after = *this;
  after.closed_ = true;
  if (in_play_) {
    after.in_play_ = false;
    after.ended_by_close_ = true;
    after.score_sheet_.push_back({players_[1 - seat], Ending::kAbandoned});
  }
  return after;
}

int Table::playerIndex(const std::string& id) const {
  const auto it = std::find(players_.begin(), players_.end(), id);
  return it == players_.end() ? -1 : static_cast<int>(it - players_.begin());
}

}  // namespace pong
