#include "domains/games/apis/one_d4_worker/metrics.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "absl/strings/str_cat.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace one_d4_worker {
namespace {

using ::futility::otel::CapturingMetricsRecorder;
using ::testing::Not;

const std::map<std::string, std::string> kCpp = {{"indexer", "cpp"}};

std::map<std::string, std::string> Cpp(const std::string& key, const std::string& value) {
  return {{"indexer", "cpp"}, {key, value}};
}

IndexedGame AGame() {
  IndexedGame game;
  game.url = "https://chess.com/game/1";
  one_d4::MotifOccurrence check;
  check.motif = one_d4::Motif::kCheck;
  one_d4::MotifOccurrence pin;
  pin.motif = one_d4::Motif::kPin;
  game.occurrences = {check, pin, check};
  return game;
}

TEST(WorkerMetrics, LabelsEverySeriesWithTheWorkerThatWroteIt) {
  // Two indexers fill these series. Unlabelled, a dashboard cannot tell a
  // C++ run that indexed nothing from a Java one that was never scheduled.
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.RunFinished(RunOutcome::kCompleted, absl::Seconds(3));
  metrics.ArchiveFetched("ok");
  metrics.MonthFinished("indexed", 12);
  metrics.GameIndexed(AGame());

  for (const CapturingMetricsRecorder::Entry& entry : recorder.Entries()) {
    const auto indexer = entry.attributes.find("indexer");
    ASSERT_NE(indexer, entry.attributes.end()) << entry.name << " carries no indexer label";
    EXPECT_EQ(indexer->second, "cpp") << entry.name;
  }
}

TEST(WorkerMetrics, CountsARunByItsOutcome) {
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.RunFinished(RunOutcome::kLeaseLost, absl::Seconds(3));

  EXPECT_EQ(recorder.CounterTotal(kRunsMetric, Cpp("outcome", "lease_lost")), 1);
  EXPECT_EQ(recorder.CounterTotal(kRunsMetric, Cpp("outcome", "completed")), 0)
      << "a run that lost its range is not a run that completed";
  EXPECT_EQ(recorder.ObservationCount(kRunDurationMetric, Cpp("outcome", "lease_lost")), 1);
}

TEST(WorkerMetrics, CountsAMonthsGamesAndItsShape) {
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.MonthFinished("indexed", 12);

  EXPECT_EQ(recorder.CounterTotal(kMonthsMetric, Cpp("result", "indexed")), 1);
  EXPECT_EQ(recorder.CounterTotal(kGamesIndexedMetric, kCpp), 12);
  EXPECT_EQ(recorder.ObservationCount(kGamesPerMonthMetric, kCpp), 1);
}

TEST(WorkerMetrics, LeavesQuietMonthsOutOfTheMonthSizeDistribution) {
  // A decade-long backfill of a three-year player is mostly empty
  // archives. Feeding those zeros in makes the average archive look a
  // third its real size; the empty months are counted on their own.
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.MonthFinished("empty", 0);

  EXPECT_EQ(recorder.CounterTotal(kMonthsMetric, Cpp("result", "empty")), 1);
  EXPECT_EQ(recorder.ObservationCount(kGamesPerMonthMetric, kCpp), 0);
}

TEST(WorkerMetrics, CountsOccurrencesByMotif) {
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.GameIndexed(AGame());

  EXPECT_EQ(recorder.CounterTotal(kMotifOccurrencesMetric, Cpp("motif", "check")), 2);
  EXPECT_EQ(recorder.CounterTotal(kMotifOccurrencesMetric, Cpp("motif", "pin")), 1);
}

TEST(WorkerMetrics, PutsNothingUnboundedInALabel) {
  // A game url or a player name here is one stored series per game, which
  // is how a metric becomes an outage.
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.GameIndexed(AGame());
  metrics.MonthFinished("indexed", 1);

  for (const CapturingMetricsRecorder::Entry& entry : recorder.Entries()) {
    for (const auto& [key, value] : entry.attributes) {
      EXPECT_THAT(value, Not(::testing::HasSubstr("chess.com/game")))
          << entry.name << " labels by " << key;
    }
  }
}

TEST(WorkerMetrics, DeclaresItsSeriesBeforeAnythingHappens) {
  // A counter created lazily by its first event exports that event as its
  // first sample, and rate() has nothing earlier to measure from — the
  // panel reads zero forever (#1323).
  CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);

  metrics.Declare();

  EXPECT_EQ(recorder.CounterTotal(kRunsMetric, Cpp("outcome", "completed")), 0);
  EXPECT_EQ(recorder.CounterTotal(kArchiveFetchesMetric, Cpp("result", "error")), 0);
  EXPECT_TRUE(recorder.Declared(kRunsMetric, Cpp("outcome", "completed")));
  EXPECT_TRUE(recorder.Declared(kRunsMetric, Cpp("outcome", "lease_lost")));
  EXPECT_TRUE(recorder.Declared(kMonthsMetric, Cpp("result", "empty")));
  EXPECT_TRUE(recorder.Declared(kArchiveFetchesMetric, Cpp("result", "error")));
  EXPECT_TRUE(recorder.Declared(kGamesIndexedMetric, kCpp));
  EXPECT_FALSE(recorder.Declared(kMonthsMetric, Cpp("result", "cached")))
      << "this worker has no period cache to hit, so the series would be a permanent zero";
}

// --- the names are the Java worker's ---------------------------------------

std::string JavaSource() {
  std::ifstream file(
      "domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java");
  EXPECT_TRUE(file.good()) << "IndexWorker.java is not where this test looks";
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

TEST(WorkerMetrics, WritesTheSeriesTheJavaWorkerWrites) {
  // Both workers fill the same series. A name that drifts does not fail
  // anything — it quietly splits one chart into two, one of them empty.
  const std::string java = JavaSource();
  for (const std::string& name :
       {std::string(kRunsMetric), std::string(kGamesIndexedMetric), std::string(kMonthsMetric),
        std::string(kArchiveFetchesMetric), std::string(kMotifOccurrencesMetric),
        std::string(kRunDurationMetric), std::string(kGamesPerMonthMetric)}) {
    EXPECT_THAT(java, ::testing::HasSubstr(absl::StrCat("\"", name, "\"")))
        << name << " is not a name IndexWorker.java uses";
  }
}

TEST(WorkerMetrics, SpellsTheRunOutcomesTheJavaWorkerSpells) {
  // The outcome label is shared too: a run this worker calls lease_lost
  // and the other calls lost_lease is two series nobody sums.
  const std::string java = JavaSource();
  const auto start = java.find("RUN_OUTCOMES =");
  ASSERT_NE(start, std::string::npos) << "RUN_OUTCOMES is gone from IndexWorker";
  const std::string list = java.substr(start, java.find(';', start) - start);

  for (const RunOutcome outcome : {RunOutcome::kCompleted, RunOutcome::kFailed,
                                   RunOutcome::kInterrupted, RunOutcome::kLeaseLost}) {
    EXPECT_THAT(list, ::testing::HasSubstr(absl::StrCat("\"", ToString(outcome), "\"")))
        << ToString(outcome) << " is not an outcome the Java worker reports";
  }
}

}  // namespace
}  // namespace one_d4_worker
