#include "domains/games/apis/one_d4_worker/metrics.h"

#include <map>
#include <string>

#include "absl/strings/ascii.h"
#include "domains/games/libs/one_d4_motifs/motif.h"

namespace one_d4_worker {
namespace {

using Labels = std::map<std::string, std::string>;

Labels With(const std::string& key, const std::string& value) {
  return {{kIndexerLabel, kIndexerValue}, {key, value}};
}

Labels Bare() { return {{kIndexerLabel, kIndexerValue}}; }

/// Only what this worker can emit. Declaring a value it never writes
/// exports a zero series forever, which reads as an answer.
constexpr std::string_view kRunOutcomes[] = {"completed", "failed", "interrupted", "lease_lost"};
constexpr std::string_view kMonthResults[] = {"indexed", "degraded", "empty"};
constexpr std::string_view kArchiveResults[] = {"ok", "no_archive", "error"};

}  // namespace

void WorkerMetrics::Declare() {
  metrics_.DeclareCounter(kGamesIndexedMetric, Bare());
  for (std::string_view outcome : kRunOutcomes) {
    metrics_.DeclareCounter(kRunsMetric, With("outcome", std::string(outcome)));
  }
  for (std::string_view result : kMonthResults) {
    metrics_.DeclareCounter(kMonthsMetric, With("result", std::string(result)));
  }
  for (std::string_view result : kArchiveResults) {
    metrics_.DeclareCounter(kArchiveFetchesMetric, With("result", std::string(result)));
  }
}

void WorkerMetrics::RunFinished(RunOutcome outcome, absl::Duration elapsed) {
  const Labels labels = With("outcome", std::string(ToString(outcome)));
  metrics_.RecordCounter(kRunsMetric, 1, labels);
  metrics_.RecordLatency(kRunDurationMetric, absl::ToChronoMicroseconds(elapsed), labels);
}

void WorkerMetrics::ArchiveFetched(std::string_view result) {
  metrics_.RecordCounter(kArchiveFetchesMetric, 1, With("result", std::string(result)));
}

void WorkerMetrics::MonthFinished(std::string_view result, int games) {
  metrics_.RecordCounter(kMonthsMetric, 1, With("result", std::string(result)));
  if (result == "empty") {
    // A decade-long backfill of a three-year player is mostly empty
    // archives, and feeding those zeros in makes the average archive look
    // a third its real size. The empty months are counted above.
    return;
  }
  metrics_.RecordCounter(kGamesIndexedMetric, games, Bare());
  metrics_.RecordDistribution(kGamesPerMonthMetric, games, Bare());
}

void WorkerMetrics::GameIndexed(const IndexedGame& game) {
  // Counted per game rather than at flush time, because a flush batches by
  // request and loses the per-motif breakdown. The detector set is fixed
  // and small, so the cardinality is bounded by the code.
  std::map<std::string, int64_t> by_motif;
  for (const one_d4::MotifOccurrence& occurrence : game.occurrences) {
    ++by_motif[absl::AsciiStrToLower(one_d4::ToString(occurrence.motif))];
  }
  for (const auto& [motif, count] : by_motif) {
    metrics_.RecordCounter(kMotifOccurrencesMetric, count, With("motif", motif));
  }
}

}  // namespace one_d4_worker
