#include "domains/games/apis/one_d4_worker/worker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// One game a month, so a run has an opponent to look up.
ArchivedGame AGame(std::string url) {
  ArchivedGame game;
  game.url = std::move(url);
  game.pgn = "1. e4 e5 1-0";
  game.white_username = "alice";
  game.black_username = "bob";
  game.time_class = "blitz";
  game.white_result = "win";
  game.black_result = "checkmated";
  game.end_time = 1700000000;
  return game;
}

class FakeArchive : public ArchiveSource {
 public:
  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view /*player*/,
                                                       YearMonth month) override {
    return months.count(month.ToString()) != 0 ? months[month.ToString()]
                                               : std::vector<ArchivedGame>{};
  }

  std::map<std::string, std::vector<ArchivedGame>> months;
};

class FakeRosters : public TitleSource {
 public:
  absl::StatusOr<std::vector<std::string>> FetchTitled(std::string_view title) override {
    ++fetches;
    const auto found = rosters.find(std::string(title));
    return found == rosters.end() ? std::vector<std::string>{} : found->second;
  }

  std::map<std::string, std::vector<std::string>> rosters;
  int fetches = 0;
};

/// What the run wrote, kept by the test rather than by the sink: a sink
/// belongs to one run and is gone when it returns.
struct Recorded {
  std::vector<std::string> jobs;
  std::vector<IndexedGame> written;
  std::vector<IndexedMonth> periods;
};

class FakeSink : public GameSink {
 public:
  explicit FakeSink(Recorded& into) : into_(into) {}

  absl::Status Write(absl::Span<const IndexedGame> games) override {
    for (const IndexedGame& game : games) into_.written.push_back(game);
    return absl::OkStatus();
  }
  absl::StatusOr<std::optional<int>> MonthAlreadyIndexed(const IndexedMonth& /*month*/) override {
    return std::nullopt;
  }
  absl::Status RecordMonth(const IndexedMonth& month) override {
    into_.periods.push_back(month);
    return absl::OkStatus();
  }

 private:
  Recorded& into_;
};

/// The factory worker_main gives MakeRun, with the connection and owner
/// swapped for a note of which job it was asked about.
SinkFactory SinksInto(Recorded& into) {
  return [&into](const IndexJob& job) {
    into.jobs.push_back(job.id);
    return std::make_unique<FakeSink>(into);
  };
}

class FakeLease : public LeaseKeeper {
 public:
  bool Keep() override { return true; }
  bool Report(int /*games_indexed*/) override { return true; }
  bool OutOfTime() override { return false; }
};

IndexJob AJob(std::string id) {
  IndexJob job;
  job.id = std::move(id);
  job.player = "alice";
  job.platform = "chess.com";
  job.start_month = "2026-01";
  job.end_month = "2026-01";
  return job;
}

/// Hands out claims from a script, then nothing.
class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view /*owner*/,
                                                    absl::Duration /*lease*/) override {
    if (!claim_status.ok()) return claim_status;
    if (claims.empty()) return std::nullopt;
    IndexJob job = claims.front();
    claims.erase(claims.begin());
    return job;
  }
  absl::StatusOr<bool> Heartbeat(std::string_view, std::string_view, absl::Duration) override {
    return true;
  }
  absl::StatusOr<bool> Progress(std::string_view, std::string_view, int) override { return true; }
  absl::StatusOr<bool> Complete(std::string_view, std::string_view, int) override { return true; }
  absl::StatusOr<bool> Fail(std::string_view, std::string_view, std::string_view) override {
    return true;
  }
  absl::StatusOr<bool> HandBack(std::string_view, std::string_view) override { return true; }
  absl::StatusOr<bool> Release(std::string_view, std::string_view) override { return true; }

  std::vector<IndexJob> claims;
  absl::Status claim_status;
};

// ---- MakeRun: which pieces a claimed request is run against ----

TEST(MakeRun, RunsEveryJobAgainstTheOneRoster) {
  // The reason the roster is a reference and not built in here. Ten
  // requests to chess.com for the life of the process, not per claim —
  // and a per-claim roster answers every question correctly, so nothing
  // else would ever notice.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeRosters rosters;
  rosters.rosters["GM"] = {"alice"};
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun(archive, titles, SinksInto(recorded), metrics, [] { return false; });

  ASSERT_TRUE(run(AJob("first"), lease).ok());
  ASSERT_TRUE(run(AJob("second"), lease).ok());

  EXPECT_EQ(rosters.fetches, 10) << "a second job re-read the rosters";
}

TEST(MakeRun, EveryRunGetsTheRoster) {
  // Unset, the run writes NULL titles on every row and marks the month
  // complete, so the period cache carries the gap for both workers with
  // nothing to say it happened.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeRosters rosters;
  rosters.rosters["GM"] = {"alice"};
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun(archive, titles, SinksInto(recorded), metrics, [] { return false; });
  ASSERT_TRUE(run(AJob("first"), lease).ok());

  ASSERT_EQ(recorded.written.size(), 1u);
  EXPECT_EQ(recorded.written.front().white_title, "GM");
}

TEST(MakeRun, EveryRunGetsItsOwnSinkForTheJobItClaimed) {
  // A sink carries the id it fences on. One shared across jobs would
  // write the second run's games under the first run's claim.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun(archive, titles, SinksInto(recorded), metrics, [] { return false; });

  ASSERT_TRUE(run(AJob("first"), lease).ok());
  ASSERT_TRUE(run(AJob("second"), lease).ok());

  EXPECT_THAT(recorded.jobs, ElementsAre("first", "second"));
}

TEST(MakeRun, EveryRunGetsTheObserver) {
  // Unset, a run indexes correctly and exports nothing — the failure
  // that only shows up as a dashboard nobody notices went flat.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun(archive, titles, SinksInto(recorded), metrics, [] { return false; });
  ASSERT_TRUE(run(AJob("first"), lease).ok());

  EXPECT_GT(recorder.CounterTotal(kGamesIndexedMetric, {{kIndexerLabel, kIndexerValue}}), 0);
}

TEST(MakeRun, EveryRunGetsTheShutdownSwitch) {
  // Unset, a SIGTERM is noticed only between claims, so the supervisor
  // kills a run mid-month and the claim sits until the lease expires.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun(archive, titles, SinksInto(recorded), metrics, [] { return true; });
  const absl::StatusOr<RunReport> report = run(AJob("first"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  ASSERT_TRUE(report->stopped.has_value());
  EXPECT_EQ(*report->stopped, Stopped::kShutdown);
}

// ---- PollLoop ----

struct Loop {
  FakeQueue queue;
  futility::otel::CapturingMetricsRecorder recorder;
  std::vector<absl::Duration> slept;
  int runs = 0;
};

/// Runs the loop for exactly `ticks` iterations, so a test does not need
/// a clock to end one — and so a change that stops it sleeping fails
/// rather than spins.
void Drive(Loop& loop, int ticks) {
  WorkerMetrics metrics(loop.recorder);
  Poller::Options options;
  options.owner = "cpp/test";
  Poller poller(
      loop.queue,
      [&loop](const IndexJob&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        ++loop.runs;
        return RunReport{};
      },
      options);

  int seen = 0;
  PollLoop(
      poller, metrics, absl::Seconds(5), [&seen, ticks] { return ++seen > ticks; },
      [&loop](absl::Duration wait) { loop.slept.push_back(wait); });
}

TEST(PollLoop, WaitsBeforeAskingAnEmptyQueueAgain) {
  // Without it an empty queue is a spin: one round trip per iteration,
  // for as long as there is nothing to do.
  Loop loop;

  Drive(loop, 1);

  EXPECT_THAT(loop.slept, ElementsAre(absl::Seconds(5)));
  EXPECT_EQ(loop.runs, 0);
}

TEST(PollLoop, KeepsGoingAfterAQueueItCannotReach) {
  // Exiting would only have the supervisor restart us into the same
  // outage, having given up the process's claims on the way out.
  Loop loop;
  loop.queue.claim_status = absl::UnavailableError("no route to the database");

  Drive(loop, 2);

  EXPECT_EQ(loop.slept.size(), 2u) << "it gave up after the first failure";
}

TEST(PollLoop, DoesNotWaitAfterARunThatDidWork) {
  // A queue with a backlog should be drained, not sipped from every five
  // seconds.
  Loop loop;
  loop.queue.claims = {AJob("first"), AJob("second")};

  Drive(loop, 3);

  EXPECT_EQ(loop.runs, 2);
  EXPECT_THAT(loop.slept, ElementsAre(absl::Seconds(5)))
      << "it waited between two claims that were both there";
}

TEST(PollLoop, CountsEveryRunItFinished) {
  Loop loop;
  loop.queue.claims = {AJob("first")};

  Drive(loop, 1);

  EXPECT_EQ(loop.recorder.CounterTotal(kRunsMetric,
                                       {{"outcome", "completed"}, {kIndexerLabel, kIndexerValue}}),
            1);
}

TEST(PollLoop, StopsWithoutClaimingAnythingWhenItStartsShuttingDown) {
  Loop loop;
  loop.queue.claims = {AJob("first")};

  WorkerMetrics metrics(loop.recorder);
  Poller::Options options;
  options.owner = "cpp/test";
  Poller poller(
      loop.queue,
      [&loop](const IndexJob&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        ++loop.runs;
        return RunReport{};
      },
      options);

  PollLoop(
      poller, metrics, absl::Seconds(5), [] { return true; },
      [&loop](absl::Duration wait) { loop.slept.push_back(wait); });

  EXPECT_EQ(loop.runs, 0);
  EXPECT_THAT(loop.slept, IsEmpty());
}

}  // namespace
}  // namespace one_d4_worker
