#include "domains/games/libs/wordchain_race/table.h"

#include <algorithm>
#include <set>
#include <utility>

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

absl::Status CheckIds(const std::vector<std::string>& ids, std::size_t min) {
  if (ids.size() < min || ids.size() > kMaxRunners) {
    return absl::InvalidArgumentError(absl::StrCat("a race seats 1 to ", kMaxRunners));
  }
  std::set<std::string> seen;
  for (const std::string& id : ids) {
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

// The seats a table stays open with: its one, practising; two otherwise.
std::size_t SeatsToStay(const Race& race) { return race.runners().size() == 1 ? 1 : 2; }

bool Done(const Runner& runner) { return runner.left || runner.finished_ms.has_value(); }

// Finishers before the rest; among finishers fewer words, then the earlier
// finish; join order otherwise.
bool Ahead(const Runner& a, const Runner& b) {
  if (a.finished_ms.has_value() != b.finished_ms.has_value()) return a.finished_ms.has_value();
  if (!a.finished_ms.has_value()) return false;
  if (a.ladder.size() != b.ladder.size()) return a.ladder.size() < b.ladder.size();
  return *a.finished_ms < *b.finished_ms;
}

}  // namespace

absl::StatusOr<Race> Race::open(std::vector<std::string> players, Puzzle puzzle, int64_t now_ms,
                                int64_t duration_ms) {
  if (absl::Status s = CheckIds(players, 1); !s.ok()) return s;
  if (absl::Status s = CheckPuzzle(puzzle); !s.ok()) return s;
  if (duration_ms <= 0) return absl::InvalidArgumentError("a race needs a clock");
  std::vector<Runner> runners;
  for (std::string& id : players) runners.push_back(Runner{std::move(id), {}, std::nullopt, false});
  return Race(std::move(puzzle), std::move(runners), now_ms, now_ms + duration_ms, std::nullopt);
}

absl::StatusOr<Race> Race::restore(Puzzle puzzle, std::vector<Runner> runners, int64_t started_ms,
                                   int64_t deadline_ms, std::optional<Ending> ending) {
  if (absl::Status s = CheckPuzzle(puzzle); !s.ok()) return s;
  std::vector<std::string> ids;
  for (const Runner& runner : runners) ids.push_back(runner.player_id);
  if (absl::Status s = CheckIds(ids, 1); !s.ok()) return s;
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
    // A finish may read before the start: the start and the play can be
    // stamped by different hosts' clocks. Never after the deadline.
    const bool reached = last == puzzle.target;
    if (reached != runner.finished_ms.has_value() ||
        (reached && *runner.finished_ms > deadline_ms)) {
      return absl::InvalidArgumentError(absl::StrCat(runner.player_id, "'s finish disagrees"));
    }
  }
  Race race(std::move(puzzle), std::move(runners), started_ms, deadline_ms, std::nullopt);
  // Play ends a race as kFinished exactly when it settles, and a race in
  // play has not. The clock and a close end only one that had not, but a
  // leave afterwards can settle it still: so a timeout or a leave may sit
  // beside runners who are all done.
  const bool settled = race.settled().has_value();
  if ((!ending.has_value() && settled) || (ending == Ending::kFinished && !settled)) {
    return absl::InvalidArgumentError("the ending disagrees with the runners");
  }
  race.ending_ = ending;
  return race;
}

int Race::runnerOf(int seat) const {
  if (seat < 0) return -1;
  for (std::size_t i = 0; i < runners_.size(); ++i) {
    if (!runners_[i].left && seat-- == 0) return static_cast<int>(i);
  }
  return -1;
}

std::optional<Ending> Race::settled() const {
  const bool on_par = std::any_of(runners_.begin(), runners_.end(), [&](const Runner& r) {
    return r.finished_ms.has_value() && static_cast<int>(r.ladder.size()) <= puzzle_.optimal - 1;
  });
  if (on_par || std::all_of(runners_.begin(), runners_.end(), Done)) return Ending::kFinished;
  return std::nullopt;
}

absl::StatusOr<int> Race::playable(int seat, int64_t now_ms) const {
  const int i = runnerOf(seat);
  if (i < 0) return absl::InvalidArgumentError("no such runner");
  if (isOver()) return absl::FailedPreconditionError("the race is over");
  if (now_ms >= deadline_ms_) return absl::FailedPreconditionError("time is up");
  if (runners_[i].finished_ms.has_value()) {
    return absl::FailedPreconditionError("already finished");
  }
  return i;
}

absl::StatusOr<Race> Race::play(int seat, std::string_view word, const Dictionary& dictionary,
                                int64_t now_ms) const {
  auto i = playable(seat, now_ms);
  if (!i.ok()) return i.status();
  const std::string lower = absl::AsciiStrToLower(word);
  if (!dictionary.contains(lower)) {
    return absl::InvalidArgumentError(absl::StrCat("\"", lower, "\" is not a word here"));
  }
  const Runner& runner = runners_[*i];
  if (!OneLetterApart(LastWord(puzzle_, runner), lower)) {
    return absl::InvalidArgumentError(
        absl::StrCat("\"", lower, "\" is not one letter from \"", LastWord(puzzle_, runner), "\""));
  }
  if (Holds(puzzle_, runner, lower)) {
    return absl::InvalidArgumentError(absl::StrCat("\"", lower, "\" is already on your ladder"));
  }
  Race next = *this;
  Runner& moved = next.runners_[*i];
  moved.ladder.push_back(lower);
  if (lower == puzzle_.target) moved.finished_ms = now_ms;
  next.ending_ = next.settled();
  return next;
}

absl::StatusOr<Race> Race::retract(int seat, int64_t now_ms) const {
  auto i = playable(seat, now_ms);
  if (!i.ok()) return i.status();
  if (runners_[*i].ladder.empty()) return absl::FailedPreconditionError("nothing to take back");
  Race next = *this;
  next.runners_[*i].ladder.pop_back();
  return next;
}

absl::StatusOr<Race> Race::leave(int seat) const {
  const int i = runnerOf(seat);
  if (i < 0) return absl::InvalidArgumentError("no such runner");
  Race next = *this;
  next.runners_[i].left = true;
  if (!next.isOver()) next.ending_ = next.settled();
  return next;
}

bool Race::timeUp(int64_t now_ms) const { return !isOver() && now_ms >= deadline_ms_; }

absl::StatusOr<Race> Race::expire(int64_t now_ms) const {
  if (!timeUp(now_ms)) return absl::FailedPreconditionError("time remains");
  Race next = *this;
  next.ending_ = Ending::kTimeout;
  return next;
}

Race Race::endByLeave() const {
  Race next = *this;
  if (!next.isOver()) next.ending_ = Ending::kLeave;
  return next;
}

std::vector<std::string> Race::winners() const {
  if (!isOver()) return {};
  const auto best = std::min_element(runners_.begin(), runners_.end(), Ahead);
  if (best == runners_.end() || !best->finished_ms.has_value()) return {};
  // Level with the best: neither ahead of the other.
  std::vector<std::string> ids;
  for (const Runner& runner : runners_) {
    if (!Ahead(*best, runner)) ids.push_back(runner.player_id);
  }
  return ids;
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
  for (const Runner& runner : runners_) {
    if (!runner.left) ids.push_back(runner.player_id);
  }
  return ids;
}

int Race::playerIndex(std::string_view id) const {
  int seat = 0;
  for (const Runner& runner : runners_) {
    if (runner.left) continue;
    if (runner.player_id == id) return seat;
    ++seat;
  }
  return -1;
}

RaceScore ScoreOf(const Race& finished) {
  return RaceScore{finished.winners(), finished.ending().value_or(Ending::kFinished)};
}

absl::StatusOr<Table> Table::open(std::vector<std::string> players, Puzzle puzzle, int64_t now_ms,
                                  int64_t duration_ms) {
  auto race = Race::open(std::move(players), std::move(puzzle), now_ms, duration_ms);
  if (!race.ok()) return race.status();
  return Table(*std::move(race), {}, false, false);
}

absl::StatusOr<Table> Table::restore(Race race, std::vector<RaceScore> score_sheet, bool closed,
                                     bool ended_by_close) {
  // Open with the seats to stay, closed with fewer: removePlayer's rule.
  if (closed != (race.players().size() < SeatsToStay(race))) {
    return absl::InvalidArgumentError("the table's seats disagree with its being closed");
  }
  if (closed && !race.isOver()) return absl::InvalidArgumentError("a closed table's race is over");
  if (race.ending() == Ending::kLeave && !closed) {
    return absl::InvalidArgumentError("only a close ends a race by leave");
  }
  if (race.isOver() && (score_sheet.empty() || score_sheet.back() != ScoreOf(race))) {
    return absl::InvalidArgumentError("the score sheet disagrees with the race");
  }
  if (ended_by_close && !closed) {
    return absl::InvalidArgumentError("only a closed table's close ended a race");
  }
  return Table(std::move(race), std::move(score_sheet), closed, ended_by_close);
}

absl::StatusOr<Table> Table::inRace(
    const std::function<absl::StatusOr<Race>(const Race&)>& change) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (race_.isOver()) return absl::FailedPreconditionError("between races");
  auto after = change(race_);
  if (!after.ok()) return after.status();
  Table next(*std::move(after), score_sheet_, false, false);
  if (next.race_.isOver()) next.score_sheet_.push_back(ScoreOf(next.race_));
  return next;
}

absl::StatusOr<Table> Table::next(Puzzle puzzle, int64_t now_ms, int64_t duration_ms) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  if (!race_.isOver()) return absl::FailedPreconditionError("a race is in play");
  auto race = Race::open(race_.players(), std::move(puzzle), now_ms, duration_ms);
  if (!race.ok()) return race.status();
  return Table(*std::move(race), score_sheet_, false, false);
}

absl::StatusOr<Table> Table::removePlayer(int seat) const {
  if (closed_) return absl::FailedPreconditionError("the table is closed");
  const bool was_over = race_.isOver();
  auto race = race_.leave(seat);
  if (!race.ok()) return race.status();
  Table next(*std::move(race), score_sheet_, false, false);
  if (next.race_.players().size() < SeatsToStay(next.race_)) {
    next.closed_ = true;
    next.ended_by_close_ = !was_over;
    next.race_ = next.race_.endByLeave();
  }
  if (!was_over && next.race_.isOver()) next.score_sheet_.push_back(ScoreOf(next.race_));
  return next;
}

}  // namespace wordchain_race
