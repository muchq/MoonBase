#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/games/apis/one_d4_worker/job.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/title_roster.h"

namespace one_d4_worker {

/// What a run did, for whoever counts it. Default no-ops so a test that
/// does not care about metrics does not have to say so.
class RunObserver {
 public:
  virtual ~RunObserver() = default;

  /// `result` is "ok", "no_archive", or "error".
  virtual void ArchiveFetched([[maybe_unused]] std::string_view result) {}
  /// `result` is "indexed", "degraded", "empty", or "cached".
  virtual void MonthFinished([[maybe_unused]] std::string_view result, [[maybe_unused]] int games) {
  }
  virtual void GameIndexed([[maybe_unused]] const IndexedGame& game) {}
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

    /// Who holds a title. Unset resolves none, which is not a
    /// degradation — a caller that does not want titles is not a caller
    /// that failed to read them.
    TitleRoster* titles = nullptr;
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
  static IndexedMonth Period(const IndexJob& job, YearMonth month, int64_t fetched_at, int games,
                             bool nothing_degraded);
  absl::Status RecordMonth(const IndexJob& job, YearMonth month, int64_t fetched_at, int games,
                           bool nothing_degraded);
  /// Records why a refused checkpoint ends the run. Past the ceiling it
  /// is this run's own clock running out, so the range goes back with the
  /// attempt spent; otherwise the range has changed hands and this run
  /// reports nothing about it.
  static void GiveUp(LeaseKeeper& lease, RunReport& report);
  RunObserver& observer();
  absl::Status Flush(std::vector<IndexedGame>& batch);
  /// "" for untitled, and for a roster nobody could read — which also
  /// marks the month incomplete, since the row is then missing something
  /// it should carry.
  std::string TitleOf(std::string_view player, bool& complete);

  ArchiveSource& archive_;
  GameSink& sink_;
  Options options_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_INDEX_RUN_H
