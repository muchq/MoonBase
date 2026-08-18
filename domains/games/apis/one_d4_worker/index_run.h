#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/poller.h"

namespace one_d4_worker {

/// What a run did, for whoever counts it. Default no-ops so a test that
/// does not care about metrics does not have to say so.
class RunObserver {
 public:
  virtual ~RunObserver() = default;

  /// `result` is "ok", "no_archive", or "error".
  virtual void ArchiveFetched(std::string_view result) {}
  /// `result` is "indexed" or "empty".
  virtual void MonthFinished(std::string_view result, int games) {}
  virtual void GameIndexed(const IndexedGame& game) {}
};

/// Indexes one job: every month of its range, oldest first.
///
/// Knows nothing about HTTP or SQL — both are ports — so what it does own
/// is the order: check the lease, fetch, extract, batch, write. A game
/// that will not replay is skipped, because one bad PGN in a month of four
/// hundred is a bad PGN, not a failed month.
class IndexRun {
 public:
  struct Options {
    int batch_size = 100;
    /// True when the worker is shutting down. Checked between months.
    std::function<bool()> stopping;
    RunObserver* observer = nullptr;
    /// Seconds since the epoch. Injected so a test can pin a period stamp.
    std::function<int64_t()> now;
  };

  IndexRun(ArchiveSource& archive, GameSink& sink, Options options);

  /// InvalidArgument for a range that will not parse or runs backwards;
  /// the archive's or the sink's error when either fails. Otherwise a
  /// report, including for the runs that stopped early.
  absl::StatusOr<RunReport> Execute(const IndexJob& job, LeaseKeeper& lease);

 private:
  int64_t Now() const;
  absl::Status RecordMonth(const IndexJob& job, YearMonth month, int64_t fetched_at, int games,
                           bool nothing_degraded);
  RunObserver& observer();
  absl::Status Flush(std::vector<IndexedGame>& batch);
  /// "" for untitled, and for a lookup that failed — which also marks the
  /// month incomplete, since the row is missing something it should carry.
  std::string TitleOf(std::string_view player, bool& complete);

  ArchiveSource& archive_;
  GameSink& sink_;
  Options options_;
  std::map<std::string, std::string, std::less<>> titles_;
  /// Cleared at the top of each month. See TitleOf.
  std::set<std::string, std::less<>> unreachable_titles_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H
