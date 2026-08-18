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
using ::testing::Not;

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

  absl::StatusOr<std::string> FetchTitle(std::string_view player) override {
    looked_up.push_back(std::string(player));
    if (!title_status.ok()) return title_status;
    const auto found = titles.find(std::string(player));
    return found == titles.end() ? "" : found->second;
  }

  std::map<std::string, std::vector<ArchivedGame>> months;
  absl::Status status;
  std::vector<std::string> asked;

  std::map<std::string, std::string> titles;
  absl::Status title_status;
  std::vector<std::string> looked_up;
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

  absl::Status RecordMonth(const IndexedMonth& month) override {
    if (!month_status.ok()) return month_status;
    periods.push_back(month);
    return absl::OkStatus();
  }

  std::vector<IndexedGame> written;
  std::vector<int> batches;
  absl::Status status;

  std::vector<IndexedMonth> periods;
  absl::Status month_status;
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

/// Well after any month these tests index, so a period is "the month is
/// over" unless a test says otherwise.
constexpr int64_t kLongAfter = 1800000000;  // 2027-01-15

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

IndexRun::Options Options() {
  IndexRun::Options options;
  options.now = [] { return kLongAfter; };
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
  EXPECT_EQ(game.indexed_at, kLongAfter) << "the worker's clock, not the database's";
  EXPECT_EQ(game.num_moves, 4);
  EXPECT_FALSE(game.occurrences.empty()) << "the motifs are the point of indexing it";
}

TEST(IndexRun, IndexesAQuietMonthAsNoGamesRatherThanSkippingIt) {
  // chess.com serves a month the player did not play as 200 with an empty
  // games list. It was read; the answer is "none".
  FakeArchive archive;
  archive.months["2026-01"] = {};
  archive.months["2026-02"] = {AGame("g2")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-02"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_THAT(archive.asked, ElementsAre("2026-01", "2026-02"));
  EXPECT_EQ(report->games_indexed, 1);
}

TEST(IndexRun, FailsAMonthWhoseArchiveIsNotThere) {
  // A 404 is a missing player or an upstream failure on a listed archive,
  // never a quiet month (#1360). Completing the request on it would stamp
  // "indexed, no games" on a month nobody read — and the Java worker,
  // writing the same rows, fails it.
  FakeArchive archive;
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-02"), lease);

  EXPECT_EQ(report.status().code(), absl::StatusCode::kNotFound);
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

TEST(IndexRun, RecordsAGameItCannotReplayRatherThanDroppingIt) {
  // One unplayable game in a month of four hundred must not fail the
  // month — and must not vanish from it either. Its players, result and
  // PGN are all readable, and the Java worker writes the row with no
  // motifs and no moves. Dropped here, the row exists on one indexer and
  // not the other, and games_count agrees with neither.
  FakeArchive archive;
  ArchivedGame broken = AGame("g-broken");
  broken.pgn = "[White \"alice\"]\n[Black \"bob\"]\n\n1. e4 e5 2. Qh6 *\n";
  archive.months["2026-01"] = {AGame("g1"), broken, AGame("g2")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->games_indexed, 3);
  ASSERT_EQ(sink.written.size(), 3u);
  EXPECT_EQ(sink.written[1].url, "g-broken");
  EXPECT_EQ(sink.written[1].num_moves, 0);
  EXPECT_THAT(sink.written[1].occurrences, IsEmpty());
  EXPECT_EQ(sink.written[1].result, "1-0") << "the metadata is readable either way";
  EXPECT_THAT(sink.written[0].occurrences, Not(IsEmpty())) << "the good games still extract";
}

TEST(IndexRun, RecordsAGameWhosePgnWillNotEvenParse) {
  FakeArchive archive;
  ArchivedGame garbage = AGame("g-garbage");
  garbage.pgn = "this is not a pgn at all";
  archive.months["2026-01"] = {garbage};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  ASSERT_EQ(sink.written.size(), 1u);
  EXPECT_EQ(sink.written[0].num_moves, 0);
  EXPECT_EQ(sink.written[0].eco, "") << "no headers to read a code out of";
  EXPECT_EQ(sink.written[0].opening_name, "Kings Pawn Opening Wayward Queen Attack")
      << "the ECOUrl comes from the archive, not the PGN";
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

TEST(IndexRun, StopsRatherThanFailsWhenTheSinkRefusesTheFence) {
  // The sink checks ownership in the transaction that writes, because a
  // check before the call is a snapshot the takeover can commit inside. A
  // refusal is the same news as a lost lease, arriving from the one place
  // that can see it without a race.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-02"] = {AGame("g2")};
  FakeSink sink;
  sink.status = absl::FailedPreconditionError("the request names another owner");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob("2026-01", "2026-02"), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(archive.asked, ElementsAre("2026-01")) << "the second month is somebody else's now";
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

TEST(IndexRun, RecordsEveryMonthItRead) {
  // Without the period row the API reports the month as missing, however
  // many games landed — DataAvailabilityResolver reads a missing row as
  // "gone", not as "look again".
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2")};
  archive.months["2026-02"] = {};
  FakeSink sink;
  FakeLease lease;

  IndexJob job = AJob("2026-01", "2026-02");
  job.exclude_bullet = true;
  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(job, lease).ok());

  ASSERT_EQ(sink.periods.size(), 2u);
  EXPECT_EQ(sink.periods[0].player, "alice");
  EXPECT_EQ(sink.periods[0].platform, "chess.com");
  EXPECT_EQ(sink.periods[0].month, "2026-01");
  EXPECT_EQ(sink.periods[0].games, 2);
  EXPECT_TRUE(sink.periods[0].complete);
  EXPECT_TRUE(sink.periods[0].exclude_bullet);
  EXPECT_EQ(sink.periods[0].fetched_at, kLongAfter);
  EXPECT_EQ(sink.periods[1].month, "2026-02");
  EXPECT_EQ(sink.periods[1].games, 0) << "a quiet month was read, and the answer was none";

  // The other arm: the period cache is keyed by the filter, so a run that
  // kept bullet games must not file under the key of one that dropped them.
  FakeSink with_bullet;
  FakeLease again;
  IndexRun keeps(archive, with_bullet, Options());
  ASSERT_TRUE(keeps.Execute(AJob("2026-01", "2026-02"), again).ok());
  ASSERT_EQ(with_bullet.periods.size(), 2u);
  EXPECT_FALSE(with_bullet.periods[0].exclude_bullet);
}

TEST(IndexRun, RecordsAMonthNotYetOverAsIncomplete) {
  // A period is complete only once the month itself is over. Stored
  // complete, the current month is skipped by every later request — the
  // Java worker honours the flag — so games played after this run are
  // never indexed until retention sweeps the row.
  FakeArchive archive;
  archive.months["2026-08"] = {AGame("g1")};
  FakeSink sink;
  FakeLease lease;

  IndexRun::Options options = Options();
  options.now = [] { return 1755000000; };  // 2025-08-12, mid-month
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob("2026-08", "2026-08"), lease).ok());

  ASSERT_EQ(sink.periods.size(), 1u);
  EXPECT_FALSE(sink.periods[0].complete);
}

TEST(IndexRun, RecordsAMonthCompleteOnceItIsOver) {
  FakeArchive archive;
  archive.months["2026-08"] = {AGame("g1")};
  FakeSink sink;
  FakeLease lease;

  IndexRun::Options options = Options();
  // The first instant of September: the month is over, barely.
  options.now = [] { return 1788220800; };
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob("2026-08", "2026-08"), lease).ok());

  ASSERT_EQ(sink.periods.size(), 1u);
  EXPECT_TRUE(sink.periods[0].complete);
}

TEST(IndexRun, RecordsAMonthIncompleteWhenSomethingCouldNotBeRead) {
  // Complete means "this row carries everything it should". A title that
  // never arrived makes the month worth refetching, and saying otherwise
  // caches the gap until retention sweeps it.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.title_status = absl::UnavailableError("profile lookup failed");
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  ASSERT_EQ(sink.periods.size(), 1u);
  EXPECT_FALSE(sink.periods[0].complete);
}

TEST(IndexRun, TellsTheObserverAMonthWasDegraded) {
  // index_months{result="degraded"} is a declared series. Counted as
  // "indexed", it is a permanent zero and nothing shows that a month went
  // in missing something.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.title_status = absl::UnavailableError("profile lookup failed");
  FakeSink sink;
  FakeLease lease;
  FakeObserver observer;

  IndexRun::Options options = Options();
  options.observer = &observer;
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  EXPECT_THAT(observer.months, ElementsAre("degraded:1"));
}

TEST(IndexRun, DoesNotRecordAMonthItCouldNotWrite) {
  // The period row is what makes a month look done. Stamping one over a
  // batch that rolled back caches a month that was never stored.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeSink sink;
  sink.status = absl::DataLossError("the transaction rolled back");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_FALSE(run.Execute(AJob(), lease).ok());

  EXPECT_THAT(sink.periods, IsEmpty());
}

TEST(IndexRun, DoesNotRecordAMonthWhoseLeaseWentAway) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeSink sink;
  sink.status = absl::FailedPreconditionError("the request names another owner");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(sink.periods, IsEmpty());
}

TEST(IndexRun, FailsWhenTheMonthCannotBeRecorded) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  FakeSink sink;
  sink.month_status = absl::DataLossError("indexed_periods is gone");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  EXPECT_EQ(run.Execute(AJob(), lease).status().code(), absl::StatusCode::kDataLoss);
}

TEST(IndexRun, AsksBeforeRecordingAMonthThatWroteNothing) {
  // Every game filtered out, so no batch is ever written — and the period
  // row carries no fence of its own. Without an empty write to ask the
  // question, this stamps a month over whoever holds the range now.
  FakeArchive archive;
  ArchivedGame bullet = AGame("g-bullet");
  bullet.time_class = "bullet";
  archive.months["2026-01"] = {bullet};
  FakeSink sink;
  sink.status = absl::FailedPreconditionError("the request names another owner");
  FakeLease lease;

  IndexJob job = AJob();
  job.exclude_bullet = true;
  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(job, lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(sink.periods, IsEmpty());
}

TEST(IndexRun, AsksBeforeRecordingAQuietMonth) {
  // Same hole by the other road: an empty archive writes no games either,
  // and the fetch that found it can easily outlive a lease.
  FakeArchive archive;
  archive.months["2026-01"] = {};
  FakeSink sink;
  sink.status = absl::FailedPreconditionError("the request names another owner");
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(sink.periods, IsEmpty());
}

TEST(IndexRun, CarriesThePlayersTitles) {
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.titles["alice"] = "GM";
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  ASSERT_EQ(sink.written.size(), 1u);
  EXPECT_EQ(sink.written.front().white_title, "GM");
  EXPECT_EQ(sink.written.front().black_title, "") << "untitled, and said so";
}

TEST(IndexRun, LooksUpEachPlayerOnce) {
  // Two hundred games a month against a handful of regulars is two hundred
  // profile calls if this is not cached, against an API that rate-limits.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2")};
  archive.months["2026-02"] = {AGame("g3")};
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob("2026-01", "2026-02"), lease).ok());

  EXPECT_THAT(archive.looked_up, ElementsAre("alice", "bob"));
}

TEST(IndexRun, IndexesTheMonthEvenWhenTitlesCannotBeRead) {
  // A title is decoration on a row. Losing the month over one would be the
  // tail wagging the dog.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.title_status = absl::UnavailableError("profile lookup failed");
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  const absl::StatusOr<RunReport> report = run.Execute(AJob(), lease);

  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_EQ(report->games_indexed, 1);
  EXPECT_EQ(sink.written.front().white_title, "");
}

TEST(IndexRun, DoesNotRetryAFailedTitleLookupOnEveryGameOfTheMonth) {
  // Two hundred games between two regulars while chess.com is refusing is
  // four hundred profile calls if a failure is not remembered — against
  // the API that is already refusing.
  FakeArchive archive;
  for (int i = 0; i < 4; ++i) archive.months["2026-01"].push_back(AGame(absl::StrCat("g", i)));
  archive.title_status = absl::UnavailableError("profile lookup failed");
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob(), lease).ok());

  EXPECT_THAT(archive.looked_up, ElementsAre("alice", "bob"));
}

TEST(IndexRun, RetriesATitleLookupThatFailed) {
  // A failed lookup is not an answer, so caching it would carry one bad
  // minute across a decade of backfill.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-02"] = {AGame("g2")};
  archive.title_status = absl::UnavailableError("profile lookup failed");
  FakeSink sink;
  FakeLease lease;

  IndexRun run(archive, sink, Options());
  ASSERT_TRUE(run.Execute(AJob("2026-01", "2026-02"), lease).ok());

  EXPECT_THAT(archive.looked_up, ElementsAre("alice", "bob", "alice", "bob"))
      << "a month is where the giving-up resets";
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

TEST(IndexRun, TellsTheObserverWhatEachMonthDid) {
  // A quiet month is counted as no_archive/empty rather than ok/indexed:0,
  // so a dashboard can tell a quiet player from a busy one.
  FakeArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.months["2026-02"] = {};
  archive.months["2026-03"] = {AGame("g3"), AGame("g4")};
  FakeSink sink;
  FakeLease lease;
  FakeObserver observer;

  IndexRun::Options options = Options();
  options.observer = &observer;
  IndexRun run(archive, sink, options);
  ASSERT_TRUE(run.Execute(AJob("2026-01", "2026-03"), lease).ok());

  EXPECT_THAT(observer.fetches, ElementsAre("ok", "no_archive", "ok"));
  EXPECT_THAT(observer.months, ElementsAre("indexed:1", "empty:0", "indexed:2"));
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
