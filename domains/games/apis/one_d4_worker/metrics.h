#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H

#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/platform/libs/futility/otel/metrics.h"

namespace one_d4_worker {

// The names the Java worker already writes. Both indexers fill the same
// series, so a typo here is a dashboard reading half its traffic —
// metrics_test reads these out of IndexWorker.java rather than trusting
// them. The exporter appends _total to counters, so index_runs is queried
// as index_runs_total.
inline constexpr char kRunsMetric[] = "index_runs";
inline constexpr char kGamesIndexedMetric[] = "games_indexed";
inline constexpr char kMonthsMetric[] = "index_months";
inline constexpr char kArchiveFetchesMetric[] = "chess_com_archive_fetches";
inline constexpr char kMotifOccurrencesMetric[] = "motif_occurrences";
inline constexpr char kRunDurationMetric[] = "index_run_duration_micros";
inline constexpr char kGamesPerMonthMetric[] = "index_games_per_month";

/// Which worker wrote a series. The whole point of running two against one
/// table is being able to tell them apart when one of them is wrong.
inline constexpr char kIndexerLabel[] = "indexer";
inline constexpr char kIndexerValue[] = "cpp";

/// Counts what a run did.
///
/// Labels are outcomes and motif names — bounded sets, fixed by the code.
/// A player name or a request id here would mint a stored series per user.
class WorkerMetrics : public RunObserver {
 public:
  explicit WorkerMetrics(futility::otel::MetricsRecorder& metrics) : metrics_(metrics) {}

  /// Exports every series this worker can write as zero from process
  /// start, so the first event of anything is not the sample a rate has
  /// nothing to measure from.
  void Declare();

  void RunFinished(RunOutcome outcome, absl::Duration elapsed);

  // RunObserver.
  void ArchiveFetched(std::string_view result) override;
  void MonthFinished(std::string_view result, int games) override;
  void GameIndexed(const IndexedGame& game) override;

 private:
  futility::otel::MetricsRecorder& metrics_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
