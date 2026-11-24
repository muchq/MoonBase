#ifndef DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_H
#define DOMAINS_GAMES_LIBS_WORDCHAIN_RACE_TABLE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
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
  /// Left the table. A runner who left keeps their ladder and any finish,
  /// and plays no more.
  bool left = false;
  bool operator==(const Runner&) const = default;
};

/// How a race ended.
enum class Ending {
  /// Every runner still seated finished, or one finished on par, which
  /// nobody can beat.
  kFinished,
  /// The clock ran out.
  kTimeout,
  /// Too few runners stayed to race on.
  kLeave,
};

/// A race: every runner climbs the same puzzle on their own ladder, against
/// one clock.
///
///   - A word is a dictionary word one letter from the runner's last (the
///     start, before any), never one their ladder already holds. Reaching
///     the target finishes the runner.
///   - A runner may take back their last word until they finish.
///   - The winner is the finisher with the fewest words, the earlier finish
///     breaking a tie; nobody wins if nobody finished. A finish on par
///     (optimal - 1 words) cannot be beaten, so it ends the race.
///
/// A seat is a runner's place among those still seated, the hub's roster
/// order: a runner who leaves gives up their seat but stays in the race.
class Race {
 public:
  [[nodiscard]] static absl::StatusOr<Race> open(std::vector<std::string> players, Puzzle puzzle,
                                                 int64_t now_ms, int64_t duration_ms);
  /// The full truth, for a stored row: refused unless every ladder is a
  /// chain from the start that finishes exactly when it reaches the target,
  /// and `ending` is set exactly when the race is over. The dictionary is
  /// not consulted; play already was.
  [[nodiscard]] static absl::StatusOr<Race> restore(Puzzle puzzle, std::vector<Runner> runners,
                                                    int64_t started_ms, int64_t deadline_ms,
                                                    std::optional<Ending> ending);

  [[nodiscard]] absl::StatusOr<Race> play(int seat, std::string_view word,
                                          const Dictionary& dictionary, int64_t now_ms) const;
  [[nodiscard]] absl::StatusOr<Race> retract(int seat, int64_t now_ms) const;
  /// The runner at `seat` leaves; the race ends if everyone still seated
  /// has finished.
  [[nodiscard]] absl::StatusOr<Race> leave(int seat) const;

  /// In play past its deadline: the hub's sweep ends it with expire.
  [[nodiscard]] bool timeUp(int64_t now_ms) const;
  /// Ended on the clock; refused unless timeUp(now_ms), so a replay against
  /// a newer race cannot end it.
  [[nodiscard]] absl::StatusOr<Race> expire(int64_t now_ms) const;
  /// Ended because too few runners stayed; a table closing does this.
  [[nodiscard]] Race endByLeave() const;

  [[nodiscard]] bool isOver() const { return ending_.has_value(); }
  [[nodiscard]] const std::optional<Ending>& ending() const { return ending_; }
  /// The best finisher, seated or not, once the race is over.
  [[nodiscard]] std::optional<std::string> winner() const;
  /// Finishers best first, then everyone else in the order they joined.
  [[nodiscard]] std::vector<std::string> standings() const;

  [[nodiscard]] const Puzzle& puzzle() const { return puzzle_; }
  /// Every runner, seated or not, in the order they joined.
  [[nodiscard]] const std::vector<Runner>& runners() const { return runners_; }
  [[nodiscard]] int64_t startedMs() const { return started_ms_; }
  [[nodiscard]] int64_t deadlineMs() const { return deadline_ms_; }
  /// The seated runners, by seat.
  [[nodiscard]] std::vector<std::string> players() const;
  /// `id`'s seat, or -1.
  [[nodiscard]] int playerIndex(std::string_view id) const;

 private:
  Race(Puzzle puzzle, std::vector<Runner> runners, int64_t started_ms, int64_t deadline_ms,
       std::optional<Ending> ending)
      : puzzle_(std::move(puzzle)),
        runners_(std::move(runners)),
        started_ms_(started_ms),
        deadline_ms_(deadline_ms),
        ending_(ending) {}

  /// The runners_ index of `seat`, or -1.
  [[nodiscard]] int runnerOf(int seat) const;
  [[nodiscard]] absl::StatusOr<int> playable(int seat, int64_t now_ms) const;
  /// The ending play has reached, if any: everyone seated done, or a finish
  /// on par.
  [[nodiscard]] std::optional<Ending> settled() const;

  Puzzle puzzle_;
  std::vector<Runner> runners_;
  int64_t started_ms_ = 0;
  int64_t deadline_ms_ = 0;
  std::optional<Ending> ending_;
};

/// A line of the table's score sheet: how one race ended, and its winner if
/// it had one.
struct RaceScore {
  std::optional<std::string> winner;
  Ending ending = Ending::kFinished;
  bool operator==(const RaceScore&) const = default;
};

/// The line a finished race scores.
[[nodiscard]] RaceScore ScoreOf(const Race& finished);

/// A race table: two to kMaxRunners seats racing one puzzle after another.
///
///   - A race's end is a line on the score sheet; the table stays open for
///     the next, which the hub starts on a fresh puzzle.
///   - A leave gives up the seat. With fewer than two seats left the table
///     closes, and a race in play ends by leave, its best finisher (if any)
///     winning.
class Table {
 public:
  [[nodiscard]] static absl::StatusOr<Table> open(std::vector<std::string> players, Puzzle puzzle,
                                                  int64_t now_ms, int64_t duration_ms);
  /// The full truth, for a stored row: refused unless a finished race's
  /// line ends the sheet, and the table is closed exactly when fewer than
  /// two seats remain, its race over then.
  [[nodiscard]] static absl::StatusOr<Table> restore(Race race, std::vector<RaceScore> score_sheet,
                                                     bool closed);

  /// A change to the race in play; a race it ends is scored. Refused
  /// between races and once the table is closed.
  [[nodiscard]] absl::StatusOr<Table> inRace(
      const std::function<absl::StatusOr<Race>(const Race&)>& change) const;
  /// The next race, for the seated players. Only between races.
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
