#include "domains/games/apis/one_d4_worker/index_run.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/archive.h"
#include "domains/games/apis/one_d4_worker/game_sink.h"
#include "domains/games/apis/one_d4_worker/poller.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr char kScholarsMate[] =
    "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n[ECO \"C20\"]\n\n"
    "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";

ArchivedGame AGame(std::string_view url, std::string_view pgn = kScholarsMate) {
  ArchivedGame game;
  game.url = std::string(url);
  game.pgn = std::string(pgn);
  game.time_class = "blitz";
  game.white_username = "alice";
  game.black_username = "bob";
  game.white_rating = 2800;
  game.black_rating = 2700;
  game.white_result = "win";
  game.black_result = "checkmated";
  game.eco_url = "https://www.chess.com/openings/Kings-Pawn-Opening-Wayward-Queen-Attack";
  game.end_time = 1767225600;
  return game;
}

/// Serves months from a map, and remembers what was asked for.
class FakeArchive : public ArchiveSource {
 public:
  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth(std::string_view player,
                                                       YearMonth month) override {
    asked.push_back(month.ToString());
    if (!status.ok()) return status;
    const auto found = months.find(month.ToString());
    if (found == months.end()) return absl::NotFoundError("no archive for that month");
    return found->second;
  }

  std::map<std::string, std::vector<ArchivedGame>> months;
  absl::Status status;
  std::vector<std::string> asked;
};

/// Collects what would have been written.
class FakeSink : public GameSink {
 public:
  absl::Status Write(absl::Span<const IndexedGame> games) override {
    if (!status.ok()) return status;
    batches.push_back(static_cast<int>(games.size()));
    for (const IndexedGame& game : games) written.push_back(game);
    return absl::OkStatus();
  }

  std::vector<IndexedGame> written;
  std::vector<int> batches;
  absl::Status status;
};

/// A lease that survives `holds` renewals and is gone after that.
class FakeLease : public LeaseKeeper {
 public:
  bool Keep() override { return ++kept <= holds; }
  int holds = 1000;
  int kept = 0;
};

IndexJob AJob(std::string_view start = "2026-01", std::string_view end = "2026-01") {
  IndexJob job;
  job.id = "job-1";
  job.player = "alice";
  job.platform = "chess.com";
  job.start_month = std::string(start);
  job.end_month = std::string(end);
  return job;
}

IndexRun::Options Options() {
  IndexRun::Options options;
  options.batch_size = 100;
  return options;
}

TEST(IndexRun, IndexesEveryMonthOfTheRange) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-02"] = {AGame("g2")};
  archive.months["2026-03"] = {AGame("g3")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-03"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_THAT(archive.asked, ElementsAre("2026-01", "2026-02", "2026-03"));
  EXPECT_EQ(report->games_indexed, 3);
}

TEST(IndexRun, WritesWhatItExtracted) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("https://chess.com/game/1")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  ASSERT_EQ(sink.written.size(), 1u);
  const IndexedGame& game = sink.written.front();
  EXPECT_EQ(game.url, "https://chess.com/game/1");
  EXPECT_EQ(game.white_username, "alice");
  EXPECT_EQ(game.black_username, "bob");
  EXPECT_EQ(game.white_elo, 2800);
  EXPECT_EQ(game.result, "1-0");
  EXPECT_EQ(game.eco, "C20") << "the code is the PGN tag, not the ECOUrl";
  EXPECT_EQ(game.opening_name, "Kings Pawn Opening Wayward Queen Attack");
  EXPECT_EQ(game.opening_family, "Kings Pawn Opening");
  EXPECT_EQ(game.time_class, "blitz");
  EXPECT_EQ(game.played_at, 1767225600);
  EXPECT_EQ(game.num_moves, 4);
  EXPECT_FALSE(game.occurrences.empty()) << "the motifs are the point of indexing it";
}

TEST(IndexRun, TreatsAMonthWithNoArchiveAsEmptyRatherThanBroken) {
  // A player who did not play that month is the common case, not an error.
  FakeArchive archive;
  archive.months["2026-02"] = {AGame("g2")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-02"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->games_indexed, 1);
}

TEST(IndexRun, FailsWhenTheArchiveCannotBeReached) {
  FakeArchive archive;
  archive.status = absl::UnavailableError("chess.com is down");
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  EXPECT_EQ(report.status().code(), absl::StatusCode::kUnavailable);
}

TEST(IndexRun, SkipsAGameItCannotReplayAndKeepsGoing) {
  // One unplayable game in a month of four hundred must not fail the month.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2", "[Event \"x\"]\n\n1. e4 e5 2. Qh6 *\n"),
                               AGame("g3")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->games_indexed, 2);
  EXPECT_EQ(sink.written.size(), 2u);
}

TEST(IndexRun, FlushesInBatchesRatherThanAllAtOnce) {
  FakeArchive archive;
  for (int i = 0; i < 5; ++i) archive.months["2026-01"].push_back(AGame(absl::StrCat("g", i)));
  FakeSink sink;
  FakeLease lease;

  IndexRun::Options options = Options();
  options.batch_size = 2;
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  EXPECT_THAT(sink.batches, ElementsAre(2, 2, 1));
}

TEST(IndexRun, DoesNotEvenFetchAMonthItNoLongerOwns) {
  // Another worker owns the range now. Fetching it would be a chess.com
  // call made on their behalf, and writing it would be over their work.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeSink sink;
  FakeLease lease;
  lease.holds = 0;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(archive.asked, IsEmpty());
  EXPECT_THAT(sink.written, IsEmpty());
}

TEST(IndexRun, StopsPartWayThroughAMonthWhenTheLeaseGoesAway) {
  // A month of four hundred games outlives a lease, so checking once on the
  // way in is not checking. Two renewals granted: the month, then the first
  // batch. The second batch is refused and nothing after it is written.
  FakeArchive archive;
  for (int i = 0; i < 5; ++i) archive.months["2026-01"].push_back(AGame(absl::StrCat("g", i)));
  FakeSink sink;
  FakeLease lease;
  lease.holds = 2;

  IndexRun::Options options = Options();
  options.batch_size = 2;
  IndexRun run(archive, sink, options);
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_EQ(sink.written.size(), 2u);
}

TEST(IndexRun, StopsWhenAskedTo) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-02"] = {AGame("g2")};
  FakeSink sink;
  FakeLease lease;

  IndexRun::Options options = Options();
  bool stop = false;
  options.stopping = [&stop] { return stop; };
  IndexRun run(archive, sink, options);
  stop = true;

  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-02"), lease);
  ASSERT_TRUE(report.ok()) << report.status();
  ASSERT_TRUE(report->stopped.has_value());
  EXPECT_EQ(*report->stopped, Stopped::kShutdown);
  EXPECT_THAT(archive.asked, IsEmpty());
}

TEST(IndexRun, FailsWhenTheSinkDoes) {
  // A batch that will not write is not a month that was indexed.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeSink sink;
  sink.status = absl::DataLossError("the transaction rolled back");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  EXPECT_EQ(run.Execute(AJob(), lease).status().code(), absl::StatusCode::kDataLoss);
}

TEST(IndexRun, RejectsARangeItCannotRead) {
  FakeArchive archive;
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  EXPECT_EQ(run.Execute(AJob("2026-03", "2026-01"), lease).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(IndexRun, LeavesOutBulletWhenTheRequestSaysTo) {
  FakeArchive archive;
  ArchivedGame bullet = AGame("g-bullet");
  bullet.time_class = "bullet";
  archive.months["2026-01"] = {AGame("g1"), bullet};
  FakeSink sink;
  FakeLease lease;

  IndexJob job = AJob();
  job.exclude_bullet = true;
  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(job, lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->games_indexed, 1);
  ASSERT_EQ(sink.written.size(), 1u);
  EXPECT_EQ(sink.written.front().url, "g1");
}

/// Records what the run said happened.
class FakeObserver : public RunObserver {
 public:
  void ArchiveFetched(std::string_view result) override { fetches.push_back(std::string(result)); }
  void MonthFinished(std::string_view result, int games) override {
    months.push_back(absl::StrCat(result, ":", games));
  }
  void GameIndexed(const IndexedGame& game) override { occurrences += game.occurrences.size(); }

  std::vector<std::string> fetches;
  std::vector<std::string> months;
  std::size_t occurrences = 0;
};

TEST(IndexRun, TellsTheObserverWhatEachMonthDid) {
  // An empty month and a missing one are the same row in the archive
  // counter and different rows in the month counter, because a dashboard
  // that cannot tell them apart cannot tell a quiet player from an outage.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-03"] = {};
  FakeSink sink;
  FakeLease lease;
  FakeObserver observer;

  IndexRun::Options options = Options();
  options.observer = &observer;
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob("2026-01", "2026-03"), lease).ok());

  EXPECT_THAT(observer.fetches, ElementsAre("ok", "no_archive", "ok"));
  EXPECT_THAT(observer.months, ElementsAre("indexed:1", "empty:0", "indexed:0"));
  EXPECT_GT(observer.occurrences, 0u);
}

TEST(IndexRun, CountsNothingWhenTheArchiveFails) {
  FakeArchive archive;
  archive.status = absl::UnavailableError("chess.com is down");
  FakeSink sink;
  FakeLease lease;
  FakeObserver observer;

  IndexRun::Options options = Options();
  options.observer = &observer;
  IndexRun run(archive, sink, options);
  ASSERT_FALSE(run.Execute(AJob(), lease).ok());

  EXPECT_THAT(observer.fetches, ElementsAre("error"));
  EXPECT_THAT(observer.months, IsEmpty());
}

}  // namespace
}  // namespace one_d4_worker
