#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H

#include <string_view>

#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/sweep_report.h"
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

// Cumulative run microseconds by outcome — a counter, not a histogram
// (#1452): RunFinished records it 1:1 beside kRunsMetric, so the mean run
// length is rate(this)/rate(index_runs) per outcome, both declarable at
// zero. The old histogram's buckets were read by nothing, and a histogram
// cannot be baselined without biasing exactly that mean.
inline constexpr char kRunDurationMetric[] = "index_run_duration_micros";

// The reanalysis pass's series (#1389 phase 5). New names rather than the
// index ones: a pass reads stored PGNs and an index run fetches months, and
// a dashboard that cannot tell them apart cannot say which one is failing.
inline constexpr char kReanalysisPassesMetric[] = "reanalysis_passes";
inline constexpr char kGamesReanalyzedMetric[] = "games_reanalyzed";

// The hourly sweep's series (#1424, #1356). The sweep deletes in silence, so
// "it stopped running" and "there was nothing to delete" look identical from
// outside — which is why the sweeps counter matters most: it turns a dead
// sweep into a rate hitting zero rather than into no signal at all.
inline constexpr char kRetentionSweepsMetric[] = "retention_sweeps";
inline constexpr char kRetentionRowsDeletedMetric[] = "retention_rows_deleted";
inline constexpr char kRetentionRequestsSettledMetric[] = "retention_requests_settled";

/// The tables the sweep deletes from directly. motif_occurrences is not here
/// and should not be: it goes with its games by ON DELETE CASCADE, so a label
/// for it would report zero forever.
inline constexpr std::string_view kRetentionTables[] = {"game_features", "indexed_periods",
                                                        "indexing_requests"};

/// The three ways a request stops being someone's problem. Bounded, like every
/// other label set here.
inline constexpr std::string_view kSettleArms[] = {"poisoned", "stalled", "released"};

/// A sweep either completed or it did not. Declared both ways so an error rate
/// has a denominator from process start.
inline constexpr std::string_view kSweepOutcomes[] = {"ok", "error"};

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

  /// Counts one sweep and everything it did. `outcome` is "ok" or "error" —
  /// a failed sweep still counts, or a database the sweep cannot reach looks
  /// the same as a sweep with nothing to do.
  void SweepFinished(std::string_view outcome, const SweepReport& report);

  // RunObserver.
  void ArchiveFetched(std::string_view result) override;
  void MonthFinished(std::string_view result, int games) override;
  void GameIndexed(const IndexedGame& game) override;

 private:
  futility::otel::MetricsRecorder& metrics_;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_METRICS_H
