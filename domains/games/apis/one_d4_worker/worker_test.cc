#include "domains/games/apis/one_d4_worker/worker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;

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
    ++fetches;
    return months.count(month.ToString()) != 0 ? months[month.ToString()]
                                               : std::vector<ArchivedGame>{};
  }

  std::map<std::string, std::vector<ArchivedGame>> months;
  int fetches = 0;
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
  return [&into](const Claim& claim) {
    // Both halves of the claim, because the sink is built from both: the
    // job's id names the row and the owner is what its writes fence on.
    into.jobs.push_back(absl::StrCat(claim.job.id, " as ", claim.owner));
    return std::make_unique<FakeSink>(into);
  };
}

class FakeLease : public LeaseKeeper {
 public:
  bool Keep() override { return true; }
  bool Report(int /*games_indexed*/) override { return true; }
  bool OutOfTime() override { return false; }
};

Claim AClaim(std::string id) {
  Claim claim;
  claim.owner = absl::StrCat("cpp/test/", id);
  claim.job.id = std::move(id);
  claim.job.player = "alice";
  claim.job.platform = "CHESS_COM";
  claim.job.start_month = "2026-01";
  claim.job.end_month = "2026-01";
  return claim;
}

// ---- Platform dispatch ----

TEST(MakeRun, SendsAJobToTheArchiveForItsPlatform) {
  FakeArchive chess_com;
  chess_com.months["2026-01"] = {AGame("c1")};
  FakeArchive lichess;
  lichess.months["2026-01"] = {AGame("l1")};
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun({{"CHESS_COM", &chess_com}, {"LICHESS", &lichess}}, {{"CHESS_COM", &titles}},
              SinksInto(recorded), metrics, [] { return false; });

  Claim claim = AClaim("first");
  claim.job.platform = "LICHESS";
  ASSERT_TRUE(run(claim, lease).ok());

  EXPECT_EQ(lichess.fetches, 1);
  EXPECT_EQ(chess_com.fetches, 0) << "the job went to the wrong platform's archive";
}

// A username means a different player on another platform, which is why
// player_titles is keyed on (platform, username). A single roster shared
// across platforms breaks that from the other end: the chess.com GM roster
// would answer a Lichess lookup, and a Lichess player who happens to share
// a handle with a titled chess.com player is recorded holding their title.
TEST(MakeRun, ARostersTitlesDoNotAnswerForAnotherPlatform) {
  FakeArchive chess_com;
  FakeArchive lichess;
  // Untitled on Lichess, and the game says nothing — so the run falls back.
  lichess.months["2026-01"] = {AGame("l1")};
  FakeRosters chess_com_rosters;
  // "alice" is a GM on chess.com, and the Lichess player of the same name
  // is somebody else entirely.
  chess_com_rosters.rosters["GM"] = {"alice"};
  TitleRoster chess_com_titles(chess_com_rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run = MakeRun({{"CHESS_COM", &chess_com}, {"LICHESS", &lichess}},
                                  {{"CHESS_COM", &chess_com_titles}}, SinksInto(recorded), metrics,
                                  [] { return false; });

  Claim claim = AClaim("first");
  claim.job.platform = "LICHESS";
  ASSERT_TRUE(run(claim, lease).ok());

  ASSERT_EQ(recorded.written.size(), 1u);
  EXPECT_EQ(recorded.written.front().white_title, "")
      << "a chess.com roster titled a Lichess player who only shares the name";
}

// A platform with no roster registered is not a platform whose titles
// failed to load: Lichess states them per game and has no roster endpoint
// at all, so the month is complete rather than degraded.
TEST(MakeRun, APlatformWithNoRosterIsNotADegradedOne) {
  FakeArchive lichess;
  ArchivedGame titled = AGame("l1");
  titled.white_title = "GM";
  lichess.months["2026-01"] = {titled};
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run =
      MakeRun({{"LICHESS", &lichess}}, {}, SinksInto(recorded), metrics, [] { return false; });

  Claim claim = AClaim("first");
  claim.job.platform = "LICHESS";
  ASSERT_TRUE(run(claim, lease).ok());

  ASSERT_EQ(recorded.periods.size(), 1u);
  EXPECT_TRUE(recorded.periods.front().complete);
  EXPECT_EQ(recorded.written.front().white_title, "GM");
}

// The stored spelling is the contract. IndexRequestService normalises before
// the row exists, so a registry keyed on the name a human types is keyed on
// something no claim will ever carry — and every live request misses.
TEST(MakeRun, TheKeyIsTheStoredSpellingNotAFriendlyOne) {
  FakeArchive chess_com;
  chess_com.months["2026-01"] = {AGame("c1")};
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run = MakeRun({{"chess.com", &chess_com}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });

  EXPECT_FALSE(run(AClaim("first"), lease).ok())
      << "a friendlier spelling answered a claim, hiding a key no row carries";
  EXPECT_EQ(chess_com.fetches, 0);
}

// A platform nobody registered has to fail the request, not complete it. A
// run that indexed nothing and returned ok would mark the month complete and
// cache the gap — #1360's rule, one level up.
TEST(MakeRun, AnUnknownPlatformFailsTheRunRatherThanIndexingNothing) {
  FakeArchive chess_com;
  FakeRosters rosters;
  TitleRoster titles(rosters, TitleRoster::Options{});
  futility::otel::CapturingMetricsRecorder recorder;
  WorkerMetrics metrics(recorder);
  FakeLease lease;

  Recorded recorded;
  const Poller::Run run = MakeRun({{"CHESS_COM", &chess_com}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });

  Claim claim = AClaim("first");
  claim.job.platform = "LICHESS";
  const absl::StatusOr<RunReport> report = run(claim, lease);

  EXPECT_FALSE(report.ok());
  EXPECT_THAT(std::string(report.status().message()), ::testing::HasSubstr("LICHESS"));
  EXPECT_TRUE(recorded.periods.empty()) << "an unreadable platform cached a complete month";
}

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
  const Poller::Run run = MakeRun({{"CHESS_COM", &archive}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });

  ASSERT_TRUE(run(AClaim("first"), lease).ok());
  ASSERT_TRUE(run(AClaim("second"), lease).ok());

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
  const Poller::Run run = MakeRun({{"CHESS_COM", &archive}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });
  ASSERT_TRUE(run(AClaim("first"), lease).ok());

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
  const Poller::Run run = MakeRun({{"CHESS_COM", &archive}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });

  ASSERT_TRUE(run(AClaim("first"), lease).ok());
  ASSERT_TRUE(run(AClaim("second"), lease).ok());

  EXPECT_THAT(recorded.jobs, ElementsAre("first as cpp/test/first", "second as cpp/test/second"));
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
  const Poller::Run run = MakeRun({{"CHESS_COM", &archive}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return false; });
  ASSERT_TRUE(run(AClaim("first"), lease).ok());

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
  const Poller::Run run = MakeRun({{"CHESS_COM", &archive}}, {{"CHESS_COM", &titles}},
                                  SinksInto(recorded), metrics, [] { return true; });
  const absl::StatusOr<RunReport> report = run(AClaim("first"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  ASSERT_TRUE(report->stopped.has_value());
  EXPECT_EQ(*report->stopped, Stopped::kShutdown);
}

TEST(OwnerId, NamesTheProcessReadably) {
  EXPECT_EQ(OwnerId("indexer-7", 1234), "cpp/indexer-7/1234");
}

TEST(OwnerId, StaysShortEnoughForTheColumnWhateverTheHostIsCalled) {
  // owner_id is VARCHAR(128) and pg_queue casts to ::text, so an
  // over-long id is not truncated — it fails the claim outright, every
  // time, and the worker indexes nothing. A claim appends "/" and a
  // 32-character token to this.
  const std::string owner = OwnerId(std::string(300, 'h'), 2147483647);

  EXPECT_LE(owner.size() + 1 + 32, 128u);
}

}  // namespace
}  // namespace one_d4_worker
