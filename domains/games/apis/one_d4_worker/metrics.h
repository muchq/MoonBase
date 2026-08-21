#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/platform/libs/futility/otel/metrics.h"

namespace one_d4_worker {

// The series names prom_proxy's registry queries. Stored series already
// carry them, so a renamed constant splits one chart into two and leaves
// one empty. The exporter appends _total to counters, so index_runs is
// queried as index_runs_total.
inline constexpr char kRunsMetric[] = "index_runs";
inline constexpr char kGamesIndexedMetric[] = "games_indexed";
inline constexpr char kMonthsMetric[] = "index_months";
inline constexpr char kArchiveFetchesMetric[] = "chess_com_archive_fetches";
inline constexpr char kMotifOccurrencesMetric[] = "motif_occurrences";
inline constexpr char kRunDurationMetric[] = "index_run_duration_micros";
inline constexpr char kGamesPerMonthMetric[] = "index_games_per_month";

// The reanalysis pass's series (#1389 phase 5). New names rather than the
// index ones: a pass reads stored PGNs and an index run fetches months, and
// a dashboard that cannot tell them apart cannot say which one is failing.
inline constexpr char kReanalysisPassesMetric[] = "reanalysis_passes";
inline constexpr char kGamesReanalyzedMetric[] = "games_reanalyzed";

/// Bucket bounds for kRunDurationMetric, in microseconds, spanning a
/// millisecond to six hours. Stored series already use this layout, and a
/// quantile across two layouts compares nothing — metrics_test pins the
/// vector.
///
/// The shared default set tops out at 10ms, which no index run has ever
/// finished inside. Every observation would land in the overflow bucket,
/// and histogram_quantile answers the highest *finite* bound when the rank
/// falls there — so a p95 over runs that all take minutes reads a flat
/// 10000. A broken histogram that looks like a fast one.
inline constexpr double kRunDurationBounds[] = {
    1'000,         10'000,         100'000,       1'000'000,   5'000'000,
    30'000'000,    60'000'000,     300'000'000,   900'000'000, 1'800'000'000,
    3'600'000'000, 10'800'000'000, 21'600'000'000};

/// Bucket bounds for kGamesPerMonthMetric, in games. Same layout contract
/// as above; metrics_test pins the vector.
inline constexpr double kGamesPerMonthBounds[] = {1,   2,   5,   10,  25,    50,
                                                  100, 200, 400, 800, 1'600, 3'200};

/// Which implementation wrote a series. Stored series are labelled with
/// it, so the value must stay stable for the timeline to stay one line.
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

  /// Counts a finished reanalysis pass and this owner's share of its games.
  /// The share, not the row's totals: a resumed pass finishing under a new
  /// owner reports totals the earlier owner already exported.
  void PassFinished(RunOutcome outcome, int games_processed, int games_failed);

  /// The bounds this worker's two histograms need, for OtelConfig.
  static std::map<std::string, std::vector<double>> HistogramBounds();

  // RunObserver.
  void ArchiveFetched(std::string_view result) override;
  void MonthFinished(std::string_view result, int games) override;
  void GameIndexed(const IndexedGame& game) override;

 private:
  futility::otel::MetricsRecorder& metrics_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
