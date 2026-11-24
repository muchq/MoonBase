#include "domains/games/libs/wordchain_race/table.h"

#include <algorithm>
#include <set>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace wordchain_race {
namespace {

absl::Status CheckPuzzle(const Puzzle& puzzle) {
  if (puzzle.start.empty() || puzzle.start.size() != puzzle.target.size() ||
      puzzle.start == puzzle.target || puzzle.optimal < 2) {
    return absl::InvalidArgumentError("not a puzzle");
  }
  return absl::OkStatus();
}

absl::Status CheckPlayers(const std::vector<std::string>& players, std::size_t min) {
  if (players.size() < min || players.size() > kMaxRunners) {
    return absl::InvalidArgumentError(absl::StrCat("a race seats 2 to ", kMaxRunners));
  }
  std::set<std::string> seen;
  for (const std::string& id : players) {
    if (id.empty() || !seen.insert(id).second) {
      return absl::InvalidArgumentError("runners must be named, once each");
    }
  }
  return absl::OkStatus();
}

const std::string& LastWord(const Puzzle& puzzle, const Runner& runner) {
  return runner.ladder.empty() ? puzzle.start : runner.ladder.back();
}

bool Holds(const Puzzle& puzzle, const Runner& runner, std::string_view word) {
  return word == puzzle.start ||
         std::find(runner.ladder.begin(), runner.ladder.end(), word) != runner.ladder.end();
}

// Finishers before the rest; among finishers fewer words, then the earlier
// finish; seat order otherwise.
bool Ahead(const Runner& a, const Runner& b) {
  if (a.finished_ms.has_value() != b.finished_ms.has_value()) return a.finished_ms.has_value();
  if (!a.finished_ms.has_value()) return false;
  if (a.ladder.size() != b.ladder.size()) return a.ladder.size() < b.ladder.size();
  return *a.finished_ms < *b.finished_ms;
}

}  // namespace

absl::StatusOr<Race> Race::open(std::vector<std::string> players, Puzzle puzzle, int64_t now_ms,
                                int64_t duration_ms) {
  if (absl::Status s = CheckPlayers(players, 2); !s.ok()) return s;
  if (absl::Status s = CheckPuzzle(puzzle); !s.ok()) return s;
  if (duration_ms <= 0) return absl::InvalidArgumentError("a race needs a clock");
  std::vector<Runner> runners;
  for (std::string& id : players) runners.push_back(Runner{std::move(id), {}, std::nullopt});
  return Race(std::move(puzzle), std::move(runners), now_ms, now_ms + duration_ms, false, false);
}

absl::StatusOr<Race> Race::restore(Puzzle puzzle, std::vector<Runner> runners, int64_t started_ms,
                                   int64_t deadline_ms, bool timed_out, bool abandoned) {
  if (absl::Status s = CheckPuzzle(puzzle); !s.ok()) return s;
  std::vector<std::string> ids;
  for (const Runner& runner : runners) ids.push_back(runner.player_id);
  if (absl::Status s = CheckPlayers(ids, 1); !s.ok()) return s;
  if (deadline_ms <= started_ms) return absl::InvalidArgumentError("a race needs a clock");
  for (const Runner& runner : runners) {
    std::string last = puzzle.start;
    std::set<std::string> held{puzzle.start};
    for (const std::string& word : runner.ladder) {
      if (!OneLetterApart(last, word) || !held.insert(word).second || last == puzzle.target) {
        return absl::InvalidArgumentError(absl::StrCat(runner.player_id, "'s ladder breaks"));
      }
      last = word;
    }
    const bool reached = last == puzzle.target;
    // A finish may read before the start: the start and the play can be
    // stamped by different hosts' clocks. Never after the deadline.
    if (reached != runner.finished_ms.has_value() ||
        (reached && *runner.finished_ms > deadline_ms)) {
      return absl::InvalidArgumentError(absl::StrCat(runner.player_id, "'s finish disagrees"));
    }
  }
  return Race(std::move(puzzle), std::move(runners), started_ms, deadline_ms, timed_out, abandoned);
}

absl::Status Race::playable(int seat, int64_t now_ms) const {
  if (seat < 0 || seat >= static_cast<int>(runners_.size())) {
    return absl::InvalidArgumentError("no such runner");
  }
  if (isOver()) return absl::FailedPreconditionError("the race is over");
  if (now_ms >= deadline_ms_) return absl::FailedPreconditionError("time is up");
  if (runners_[seat].finished_ms.has_value()) {
    return absl::FailedPreconditionError("already finished");
  }
  return absl::OkStatus();
}

absl::StatusOr<Race> Race::play(int seat, std::string_view word, const Dictionary& dictionary,
                                int64_t now_ms) const {
  if (absl::Status s = playable(seat, now_ms); !s.ok()) return s;
  const std::string lower = absl::AsciiStrToLower(word);
  if (!dictionary.contains(lower)) {
    return absl::InvalidArgumentError(absl::StrCat("\"", lower, "\" is not a word here"));
  }
  const Runner& runner = runners_[seat];
  if (!OneLetterApart(LastWord(puzzle_, runner), lower)) {
    return absl::InvalidArgumentError(
        absl::StrCat("\"", lower, "\" is not one letter from \"", LastWord(puzzle_, runner), "\""));
  }
  if (Holds(puzzle_, runner, lower)) {
    return absl::InvalidArgumentError(absl::StrCat("\"", lower, "\" is already on your ladder"));
  }
  Race next = *this;
  Runner& moved = next.runners_[seat];
  moved.ladder.push_back(lower);
  if (lower == puzzle_.target) moved.finished_ms = now_ms;
  return next;
}

absl::StatusOr<Race> Race::retract(int seat, int64_t now_ms) const {
  if (absl::Status s = playable(seat, now_ms); !s.ok()) return s;
  if (runners_[seat].ladder.empty()) return absl::FailedPreconditionError("nothing to take back");
  Race next = *this;
  next.runners_[seat].ladder.pop_back();
  return next;
}

absl::StatusOr<Race> Race::withoutRunner(int seat) const {
  if (seat < 0 || seat >= static_cast<int>(runners_.size())) {
    return absl::InvalidArgumentError("no such runner");
  }
  Race next = *this;
  next.runners_.erase(next.runners_.begin() + seat);
  return next;
}

bool Race::timeUp(int64_t now_ms) const { return !isOver() && now_ms >= deadline_ms_; }

Race Race::expire() const {
  Race next = *this;
  if (!isOver()) next.timed_out_ = true;
  return next;
}

Race Race::abandon() const {
  Race next = *this;
  next.abandoned_ = true;
  return next;
}

bool Race::isOver() const {
  return timed_out_ || abandoned_ ||
         std::all_of(runners_.begin(), runners_.end(),
                     [](const Runner& r) { return r.finished_ms.has_value(); });
}

std::optional<std::string> Race::winner() const {
  if (abandoned_ || !isOver()) return std::nullopt;
  const auto best = std::min_element(runners_.begin(), runners_.end(), Ahead);
  if (best == runners_.end() || !best->finished_ms.has_value()) return std::nullopt;
  return best->player_id;
}

std::vector<std::string> Race::standings() const {
  std::vector<Runner> order = runners_;
  std::stable_sort(order.begin(), order.end(), Ahead);
  std::vector<std::string> ids;
  for (const Runner& runner : order) ids.push_back(runner.player_id);
  return ids;
}

std::vector<std::string> Race::players() const {
  std::vector<std::string> ids;
  for (const Runner& runner : runners_) ids.push_back(runner.player_id);
  return ids;
}

int Race::playerIndex(std::string_view id) const {
  for (std::size_t i = 0; i < runners_.size(); ++i) {
    if (runners_[i].player_id == id) return static_cast<int>(i);
  }
  return -1;
}

absl::StatusOr<Table> Table::open(std::vector<std::string> players, Puzzle puzzle, int64_t now_ms,
                                  int64_t duration_ms) {
  auto race = Race::open(std::move(players), std::move(puzzle), now_ms, duration_ms);
  if (!race.ok()) return race.status();
  return Table(*std::move(race), {}, false);
}

absl::StatusOr<Table> Table::restore(Race race, std::vector<RaceScore> score_sheet, bool closed) {
  // Open with two seats or more, closed with fewer: removePlayer's rule.
  if (closed != (race.runners().size() < 2)) {
    return absl::InvalidArgumentError("the table's seats disagree with its being closed");
  }
  if (closed && !race.isOver()) return absl::InvalidArgumentError("a closed table's race is over");
  if (race.abandoned() && !closed) return absl::InvalidArgumentError("only a close abandons");
  // The line's winner may since have left, so the race no longer names them.
  if (race.isOver() && score_sheet.empty()) {
    return absl::InvalidArgumentError("a finished race has a line on the sheet");
  }
  return Table(std::move(race), std::move(score_sheet), closed);
}

absl::StatusOr<Table> Table::inRace(
    const std::function<absl::StatusOr<Race>(const Race&)>& change) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (race_.isOver()) return absl::FailedPreconditionError("between races");
  auto after = change(race_);
  if (!after.ok()) return after.status();
  Table next(*std::move(after), score_sheet_, false);
  if (next.race_.isOver()) next.score_sheet_.push_back(RaceScore{next.race_.winner()});
  return next;
}

absl::StatusOr<Table> Table::next(Puzzle puzzle, int64_t now_ms, int64_t duration_ms) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (!race_.isOver()) return absl::FailedPreconditionError("a race is in play");
  auto race = Race::open(race_.players(), std::move(puzzle), now_ms, duration_ms);
  if (!race.ok()) return race.status();
  return Table(*std::move(race), score_sheet_, false);
}

absl::StatusOr<Table> Table::removePlayer(int seat) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  const bool was_over = race_.isOver();
  auto race = race_.withoutRunner(seat);
  if (!race.ok()) return race.status();
  Table next(*std::move(race), score_sheet_, false);
  if (next.race_.runners().size() < 2) {
    next.closed_ = true;
    if (!was_over) {
      next.race_ = next.race_.abandon();
      next.score_sheet_.push_back(RaceScore{std::nullopt});
    }
  } else if (!was_over && next.race_.isOver()) {
    next.score_sheet_.push_back(RaceScore{next.race_.winner()});
  }
  return next;
}

}  // namespace wordchain_race
