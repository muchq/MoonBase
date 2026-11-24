#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/wordchain_race/dictionary.h"

namespace wordchain_race {

/// The most runners a race seats.
inline constexpr int kMaxRunners = 4;

/// One runner's climb: the words played after the start, in order.
struct Runner {
  std::string player_id;
  std::vector<std::string> ladder;
  /// When the ladder reached the target.
  std::optional<int64_t> finished_ms;
  bool operator==(const Runner&) const = default;
};

/// A race: every runner climbs the same puzzle on their own ladder, against
/// one clock.
///
///   - A word is a dictionary word one letter from the runner's last (the
///     start, before any), never one their ladder already holds. Reaching
///     the target finishes the runner.
///   - A runner may take back their last word until they finish.
///   - The race ends when every runner has finished, or on the clock.
///   - The winner is the finisher with the fewest words, the earlier finish
///     breaking a tie; nobody wins if nobody finished, or if the race was
///     abandoned.
class Race {
 public:
  [[nodiscard]] static absl::StatusOr<Race> open(std::vector<std::string> players, Puzzle puzzle,
                                                 int64_t now_ms, int64_t duration_ms);
  /// The full truth, for a stored row: refused unless every ladder is a
  /// chain from the start that finishes exactly when it reaches the target.
  /// The dictionary is not consulted; play already was.
  [[nodiscard]] static absl::StatusOr<Race> restore(Puzzle puzzle, std::vector<Runner> runners,
                                                    int64_t started_ms, int64_t deadline_ms,
                                                    bool timed_out, bool abandoned);

  [[nodiscard]] absl::StatusOr<Race> play(int seat, std::string_view word,
                                          const Dictionary& dictionary, int64_t now_ms) const;
  [[nodiscard]] absl::StatusOr<Race> retract(int seat, int64_t now_ms) const;
  /// The runner at `seat` gone; the race ends if everyone left has finished.
  [[nodiscard]] absl::StatusOr<Race> withoutRunner(int seat) const;

  /// Running past its deadline: the hub's sweep ends it with expire.
  [[nodiscard]] bool timeUp(int64_t now_ms) const;
  [[nodiscard]] Race expire() const;
  /// Ended with nobody winning: too few runners left to race.
  [[nodiscard]] Race abandon() const;

  [[nodiscard]] bool isOver() const;
  [[nodiscard]] bool timedOut() const { return timed_out_; }
  [[nodiscard]] bool abandoned() const { return abandoned_; }
  [[nodiscard]] std::optional<std::string> winner() const;
  /// Finishers best first, then everyone else in seat order.
  [[nodiscard]] std::vector<std::string> standings() const;

  [[nodiscard]] const Puzzle& puzzle() const { return puzzle_; }
  [[nodiscard]] const std::vector<Runner>& runners() const { return runners_; }
  [[nodiscard]] int64_t startedMs() const { return started_ms_; }
  [[nodiscard]] int64_t deadlineMs() const { return deadline_ms_; }
  [[nodiscard]] std::vector<std::string> players() const;
  [[nodiscard]] int playerIndex(std::string_view id) const;

 private:
  Race(Puzzle puzzle, std::vector<Runner> runners, int64_t started_ms, int64_t deadline_ms,
       bool timed_out, bool abandoned)
      : puzzle_(std::move(puzzle)),
        runners_(std::move(runners)),
        started_ms_(started_ms),
        deadline_ms_(deadline_ms),
        timed_out_(timed_out),
        abandoned_(abandoned) {}

  [[nodiscard]] absl::Status playable(int seat, int64_t now_ms) const;

  Puzzle puzzle_;
  std::vector<Runner> runners_;
  int64_t started_ms_ = 0;
  int64_t deadline_ms_ = 0;
  bool timed_out_ = false;
  bool abandoned_ = false;
};

/// A line of the table's score sheet: one race's winner, if it had one.
struct RaceScore {
  std::optional<std::string> winner;
  bool operator==(const RaceScore&) const = default;
};

/// A race table: two to kMaxRunners seats racing one puzzle after another.
///
///   - A race's end is a line on the score sheet; the table stays open for
///     the next, which the hub starts on a fresh puzzle.
///   - A leave drops the runner. With fewer than two seats left the table
///     closes, and a race in play ends with nobody winning.
class Table {
 public:
  [[nodiscard]] static absl::StatusOr<Table> open(std::vector<std::string> players, Puzzle puzzle,
                                                  int64_t now_ms, int64_t duration_ms);
  /// The full truth, for a stored row: refused unless a finished race has
  /// its line on the sheet, and the table is closed exactly when fewer than
  /// two seats remain, its race over and abandoned only then. The line's
  /// winner is not checked against the race: a winner who left between
  /// races is no longer in it.
  [[nodiscard]] static absl::StatusOr<Table> restore(Race race, std::vector<RaceScore> score_sheet,
                                                     bool closed);

  /// A change to the race in play; a race it ends is scored. Refused
  /// between races and once the table is closed.
  [[nodiscard]] absl::StatusOr<Table> inRace(
      const std::function<absl::StatusOr<Race>(const Race&)>& change) const;
  /// The next race. Only between races.
  [[nodiscard]] absl::StatusOr<Table> next(Puzzle puzzle, int64_t now_ms,
                                           int64_t duration_ms) const;
  [[nodiscard]] absl::StatusOr<Table> removePlayer(int seat) const;

  /// Closed: too few seats left.
  [[nodiscard]] bool isOver() const { return closed_; }
  [[nodiscard]] const Race& race() const { return race_; }
  [[nodiscard]] const std::vector<RaceScore>& scoreSheet() const { return score_sheet_; }
  [[nodiscard]] std::vector<std::string> players() const { return race_.players(); }
  [[nodiscard]] int playerIndex(std::string_view id) const { return race_.playerIndex(id); }

 private:
  Table(Race race, std::vector<RaceScore> score_sheet, bool closed)
      : race_(std::move(race)), score_sheet_(std::move(score_sheet)), closed_(closed) {}

  Race race_;
  std::vector<RaceScore> score_sheet_;
  bool closed_ = false;
};

}  // namespace wordchain_race

#endif  // DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_H
