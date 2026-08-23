#include "domains/games/apis/one_d4_worker/retention.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

// The sweep's guarantees are statements about concurrent SQL — "no worker
// anywhere holds a live lease", "a request outlives the games pointing at it"
// — so they are tested against a real Postgres. The CI job supplies one, and
// without PG_TEST_DB_URL these skip.
//
// The four tables the sweep touches, declared with the columns it keys on and
// not the rest — game_features has fifteen more in production that no arm here
// reads. So this is not a mirror of the migrations and schema_contract_test
// does not compare it against them the way it does pg_queue_test's. What it
// does pin is the part that would silently invalidate these tests:
// TheMigrationSchemaHasTheColumnsTheSweepKeysOn checks that every column named
// below still exists and that every timestamp compared against is still a
// naive TIMESTAMP.

/// A schema of this suite's own, as pg_game_sink_test does. Every suite here
/// gets the one database CI runs, and bazel runs the targets in parallel, so
/// the public schema is shared mutable state: this suite's game_features
/// carries a foreign key onto indexing_requests, which is enough to make
/// pg_queue_test's `DROP TABLE indexing_requests` fail outright — a real
/// failure in a suite that changed nothing, blaming a table it does not own.
constexpr char kSchema[] = "one_d4_retention_test";

/// search_path on the connection rather than a qualified name on every
/// statement: Sweep's SQL is the production SQL, and production does not
/// qualify.
std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema);
}

class RetentionTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    {
      pg::Client bootstrap(url);
      ASSERT_TRUE(bootstrap.Exec(absl::StrCat("CREATE SCHEMA IF NOT EXISTS ", kSchema)).ok());
    }
    client_ = std::make_unique<pg::Client>(Conninfo(url));

    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS motif_occurrences").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS game_features").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS indexed_periods").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS indexing_requests").ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE indexing_requests (
            id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            player         VARCHAR(255) NOT NULL,
            platform       VARCHAR(50) NOT NULL,
            start_month    VARCHAR(7) NOT NULL,
            end_month      VARCHAR(7) NOT NULL,
            status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
            created_at     TIMESTAMP NOT NULL DEFAULT now(),
            updated_at     TIMESTAMP NOT NULL DEFAULT now(),
            error_message  TEXT,
            games_indexed  INT DEFAULT 0,
            exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE,
            owner_id       VARCHAR(128),
            lease_expires_at TIMESTAMP,
            skip_cache     BOOLEAN DEFAULT FALSE,
            attempts       INT DEFAULT 0,
            dedupe_key     VARCHAR(600)
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE game_features (
            game_url   VARCHAR(1024) PRIMARY KEY,
            request_id UUID REFERENCES indexing_requests(id),
            indexed_at TIMESTAMP NOT NULL DEFAULT now()
        ))")
                    .ok());
    // The cascade is why the sweep does not delete motifs itself, and why the
    // metric has no label for them.
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE motif_occurrences (
            id        SERIAL PRIMARY KEY,
            game_url  VARCHAR(1024) NOT NULL
                        REFERENCES game_features(game_url) ON DELETE CASCADE
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE indexed_periods (
            id         SERIAL PRIMARY KEY,
            fetched_at TIMESTAMP NOT NULL DEFAULT now()
        ))")
                    .ok());
  }

  /// A request, with everything the arms key on stated explicitly.
  std::string Request(const std::string& name, const std::string& status, absl::Time updated,
                      const std::string& owner = "", absl::Time lease = absl::InfinitePast(),
                      int attempts = 0, absl::Time created = absl::InfinitePast()) {
    const std::string id = Id(name);
    const std::string created_at = created == absl::InfinitePast() ? Stamp(kNow) : Stamp(created);
    auto result = client_->Exec(
        R"(INSERT INTO indexing_requests
             (id, player, platform, start_month, end_month, status, created_at, updated_at,
              owner_id, lease_expires_at, attempts, dedupe_key)
           VALUES ($1::uuid, $2, 'chess.com', '2026-01', '2026-01', $3, $4::timestamp,
                   $5::timestamp, NULLIF($6, ''), NULLIF($7, '')::timestamp, $8::int, $1))",
        {id, name, status, created_at, Stamp(updated), owner,
         lease == absl::InfinitePast() ? "" : Stamp(lease), std::to_string(attempts)});
    EXPECT_TRUE(result.ok()) << result.status();
    return id;
  }

  std::string Status(const std::string& id) { return Scalar("status", id); }
  std::string Owner(const std::string& id) { return Scalar("COALESCE(owner_id, '')", id); }
  std::string Error(const std::string& id) { return Scalar("COALESCE(error_message, '')", id); }
  std::string DedupeKey(const std::string& id) { return Scalar("COALESCE(dedupe_key, '')", id); }

  int CountOf(const std::string& table) {
    auto result = client_->Exec(absl::StrCat("SELECT COUNT(*) FROM ", table));
    EXPECT_TRUE(result.ok()) << result.status();
    return std::stoi(result->Get(0, 0).value_or("0"));
  }

  static std::string Stamp(absl::Time t) {
    return absl::FormatTime("%Y-%m-%d %H:%M:%E6S", t, absl::UTCTimeZone());
  }

  /// A stable UUID per test-local name, because the real id column is one.
  static std::string Id(const std::string& name) {
    std::string hex;
    for (const char c : name) absl::StrAppend(&hex, absl::Hex(c, absl::kZeroPad2));
    hex.resize(32, '0');
    return absl::StrCat(hex.substr(0, 8), "-", hex.substr(8, 4), "-", hex.substr(12, 4), "-",
                        hex.substr(16, 4), "-", hex.substr(20, 12));
  }

  static constexpr absl::Time kNow = absl::FromUnixSeconds(1800000000);

  std::unique_ptr<pg::Client> client_;

  /// The shipped policy, loaded the way the worker loads it. Not a fixture's
  /// own numbers: a sweep tested against invented windows says nothing about
  /// the ones production runs.
  RetentionPolicy policy_ = [] {
    auto loaded = LoadRetentionPolicy("domains/games/apis/one_d4/retention_policy.json");
    EXPECT_TRUE(loaded.ok()) << loaded.status();
    return loaded.value_or(RetentionPolicy{});
  }();

 private:
  std::string Scalar(const std::string& expr, const std::string& id) {
    auto result = client_->Exec(
        absl::StrCat("SELECT ", expr, " FROM indexing_requests WHERE id = $1::uuid"), {id});
    EXPECT_TRUE(result.ok()) << result.status();
    EXPECT_EQ(result->rows(), 1) << "no row for " << id;
    return result->rows() == 1 ? result->Get(0, 0).value_or("") : "";
  }
};

/// The isolation itself, because nothing else here would notice losing it.
/// Without the search_path this suite builds its tables in public, where
/// game_features' foreign key onto indexing_requests makes pg_queue_test's
/// unqualified `DROP TABLE indexing_requests` fail — in a suite that changed
/// nothing, naming a table it does not own. These targets share the one
/// database CI runs and bazel schedules them in parallel, so the only version
/// of that bug that shows up locally is the one that already shipped.
TEST_F(RetentionTest, RunsInItsOwnSchemaRatherThanPublic) {
  const auto schema = client_->Exec("SELECT current_schema()");
  ASSERT_TRUE(schema.ok()) << schema.status();
  EXPECT_EQ(schema->Get(0, 0).value_or("(null)"), kSchema);

  // And the unqualified name production uses resolves here, which
  // current_schema() alone does not say — a search_path naming a schema that
  // does not exist falls through to public with no error. Sibling suites keep
  // an indexing_requests of their own, so the question is never "does this
  // table exist" but "which one does Sweep's SQL reach".
  const auto resolved = client_->Exec(
      "SELECT n.nspname FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace"
      " WHERE c.oid = to_regclass('indexing_requests')");
  ASSERT_TRUE(resolved.ok()) << resolved.status();
  ASSERT_EQ(resolved->rows(), 1) << "unqualified indexing_requests resolves to nothing";
  EXPECT_EQ(resolved->Get(0, 0).value_or("(null)"), kSchema);
}

TEST_F(RetentionTest, ReleasesALapsedClaimWithoutFailingIt) {
  const std::string id =
      Request("lapsed", "PROCESSING", kNow, "worker-1", kNow - absl::Minutes(1), 1);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.released, 1);
  EXPECT_EQ(report.poisoned, 0);
  EXPECT_EQ(report.stalled, 0);

  // The work goes back to the queue: no owner, still live, nothing said to the
  // user about work that is about to run.
  EXPECT_EQ(Owner(id), "");
  EXPECT_EQ(Status(id), "PROCESSING");
  EXPECT_EQ(Error(id), "");
}

TEST_F(RetentionTest, LeavesALiveClaimAlone) {
  const std::string id =
      Request("live", "PROCESSING", kNow, "worker-1", kNow + absl::Minutes(4), 1);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.settled(), 0);
  EXPECT_EQ(Owner(id), "worker-1");
}

TEST_F(RetentionTest, RetiresARequestWhoseAttemptsAreSpent) {
  const std::string id = Request("poisoned", "PENDING", kNow, "", absl::InfinitePast(), 3);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.poisoned, 1);
  EXPECT_EQ(Status(id), "FAILED");
  EXPECT_EQ(Error(id), kPoisonedMessage);
  // The key is surrendered, so the next submit for the same range starts a new
  // request rather than colliding with a dead one.
  EXPECT_EQ(DedupeKey(id), "");
}

TEST_F(RetentionTest, ARowAtTheAttemptLimitIsPoisonedRatherThanReleased) {
  // It is unheld and old enough for both retiring arms, and a held-but-lapsed
  // version would match the release arm's shape too. Only one outcome tells
  // the user why the request stopped.
  const std::string id =
      Request("spent", "PROCESSING", kNow - absl::Hours(2), "worker-1", kNow - absl::Minutes(1), 3);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.poisoned, 1);
  EXPECT_EQ(report.released, 0);
  EXPECT_EQ(report.stalled, 0);
  EXPECT_EQ(Error(id), kPoisonedMessage);
}

TEST_F(RetentionTest, RetiresAStalledRequestWhenNoWorkerHoldsALease) {
  const std::string id = Request("stalled", "PENDING", kNow - absl::Hours(2));

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.stalled, 1);
  EXPECT_EQ(Status(id), "FAILED");
  EXPECT_EQ(Error(id), kStalledMessage);
}

TEST_F(RetentionTest, ABacklogIsNotAnOutage) {
  // The row is old and unheld and would retire on age alone. It must not,
  // because another row shows a worker is running: one worker draining a deep
  // queue leaves the back of it untouched for as long as the backlog takes.
  const std::string waiting = Request("waiting", "PENDING", kNow - absl::Hours(2));
  Request("running", "PROCESSING", kNow, "worker-1", kNow + absl::Minutes(4));

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.stalled, 0);
  EXPECT_EQ(Status(waiting), "PENDING");
}

TEST_F(RetentionTest, AFleetThatDiedRecentlyIsNotAnOutageYet) {
  // The second NOT EXISTS, which the live-lease one does not cover: nobody
  // holds a lease *now*, but somebody held one half an hour ago, inside the
  // staleness window. That is a fleet restarting, not a fleet gone — and
  // retiring the queue underneath it would tell users to resubmit into a
  // system that is about to come back.
  const std::string waiting = Request("waiting", "PENDING", kNow - absl::Hours(2));
  Request("recently_held", "PROCESSING", kNow, "worker-1", kNow - absl::Minutes(30), 1);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.stalled, 0) << "a lease held inside the staleness window still vouches";
  EXPECT_EQ(Status(waiting), "PENDING");
  // The recently-held row is itself just a lapsed claim, so it goes back to
  // the queue rather than being retired.
  EXPECT_EQ(report.released, 1);
}

TEST_F(RetentionTest, ReleasingDoesNotHideStalenessFromTheSameSweep) {
  // Releasing stamps updated_at. Run before the stalled arm it would make
  // every released row look freshly touched, costing the user another whole
  // staleness window of silence. This pins the order: the row is old, unheld
  // after its own lease lapsed, and nothing else is running.
  const std::string id =
      Request("both", "PROCESSING", kNow - absl::Hours(2), "worker-1", kNow - absl::Minutes(1), 1);

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.stalled, 1) << "the stalled arm ran after the release stamped updated_at";
  EXPECT_EQ(Status(id), "FAILED");
  EXPECT_EQ(Error(id), kStalledMessage);
}

TEST_F(RetentionTest, DeletesGamesAndPeriodsPastTheWindowAndCascadesMotifs) {
  Request("owner", "COMPLETE", kNow, "", absl::InfinitePast(), 0, kNow);
  ASSERT_TRUE(client_
                  ->Exec(R"(INSERT INTO game_features (game_url, request_id, indexed_at)
                            VALUES ('old', $1::uuid, $2::timestamp), ('new', $1::uuid, $3::timestamp))",
                         {Id("owner"), Stamp(kNow - absl::Hours(24 * 8)), Stamp(kNow)})
                  .ok());
  ASSERT_TRUE(
      client_->Exec("INSERT INTO motif_occurrences (game_url) VALUES ('old'), ('new')").ok());
  ASSERT_TRUE(client_
                  ->Exec(R"(INSERT INTO indexed_periods (fetched_at)
                            VALUES ($1::timestamp), ($2::timestamp))",
                         {Stamp(kNow - absl::Hours(24 * 8)), Stamp(kNow)})
                  .ok());

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.games_deleted, 1);
  EXPECT_EQ(report.periods_deleted, 1);
  EXPECT_EQ(CountOf("game_features"), 1);
  EXPECT_EQ(CountOf("indexed_periods"), 1);
  // Not counted and not deleted directly — the FK carries them.
  EXPECT_EQ(CountOf("motif_occurrences"), 1);
}

TEST_F(RetentionTest, KeepsARequestWhileAnyGameStillPointsAtIt) {
  // Past the 30-day window, but its games are inside the 7-day one. The
  // foreign key is why the sweep may not take it yet.
  Request("referenced", "COMPLETE", kNow - absl::Hours(24 * 31), "", absl::InfinitePast(), 0,
          kNow - absl::Hours(24 * 31));
  ASSERT_TRUE(client_
                  ->Exec(R"(INSERT INTO game_features (game_url, request_id, indexed_at)
                            VALUES ('fresh', $1::uuid, $2::timestamp))",
                         {Id("referenced"), Stamp(kNow)})
                  .ok());

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.requests_deleted, 0);
  EXPECT_EQ(CountOf("indexing_requests"), 1);
}

TEST_F(RetentionTest, ARequestAndItsGamesClearInOnePass) {
  // Both past their windows. Games are deleted first, which is what lets the
  // request go in the same sweep instead of waiting for the next one.
  Request("aged", "COMPLETE", kNow - absl::Hours(24 * 31), "", absl::InfinitePast(), 0,
          kNow - absl::Hours(24 * 31));
  ASSERT_TRUE(client_
                  ->Exec(R"(INSERT INTO game_features (game_url, request_id, indexed_at)
                            VALUES ('stale', $1::uuid, $2::timestamp))",
                         {Id("aged"), Stamp(kNow - absl::Hours(24 * 31))})
                  .ok());

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.games_deleted, 1);
  EXPECT_EQ(report.requests_deleted, 1);
  EXPECT_EQ(CountOf("indexing_requests"), 0);
}

TEST_F(RetentionTest, NeverDeletesALiveRequestHoweverOld) {
  // Age is not the only condition: a PENDING row past the request window is
  // still work someone may run.
  Request("ancient", "PENDING", kNow, "worker-1", kNow + absl::Minutes(4), 0,
          kNow - absl::Hours(24 * 60));

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, kNow, report);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(report.requests_deleted, 0);
  EXPECT_EQ(CountOf("indexing_requests"), 1);
}

/// A delete that fails does not undo the settling that ran before it.
///
/// The two phases are separate transactions for exactly this. A poisoned row
/// that fails to reach FAILED keeps attempts >= kMaxAttempts, and ClaimNext
/// excludes those, so no worker will ever take it and the user is never told
/// why — it sits invisible until some later sweep gets all the way through.
/// The deletes are idempotent, so losing them costs an hour instead.
///
/// The delete is made to fail by taking its table away, which is deterministic
/// where a real statement timeout or lock wait would be a race.
TEST_F(RetentionTest, AFailedDeleteKeepsTheSettlingThatAlreadyRan) {
  const absl::Time now = absl::Now();
  const std::string spent = Request("spent", "PENDING", now - absl::Hours(2));
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET attempts = $2::int WHERE id = $1::uuid",
                         {spent, std::to_string(PgQueue::kMaxAttempts)})
                  .ok());

  ASSERT_TRUE(client_->Exec("DROP TABLE indexed_periods").ok());

  SweepReport report;
  const absl::Status swept = Sweep(*client_, policy_, now, report);
  ASSERT_FALSE(swept.ok()) << "the sweep should have failed on the missing table";

  // The report goes back with the failure, so the pass is not counted as
  // having settled nothing. A failed sweep is exactly when the arms that did
  // commit are worth knowing about.
  EXPECT_EQ(report.poisoned, 1) << "the settle counts were discarded with the delete failure";
  EXPECT_EQ(report.periods_deleted, 0);

  const auto settled =
      client_->Exec("SELECT status, attempts FROM indexing_requests WHERE id = $1::uuid", {spent});
  ASSERT_TRUE(settled.ok()) << settled.status();
  EXPECT_EQ(settled->Get(0, 0).value_or("(null)"), "FAILED")
      << "the poisoned row was rolled back by a delete that failed after it, and it is now "
         "unclaimable (attempts at the limit) and unanswered";
}

TEST_F(RetentionTest, SweepingTwiceSettlesNothingNew) {
  // Idempotent by construction, which is what makes a truncated pass cost an
  // hour rather than correctness.
  Request("lapsed", "PROCESSING", kNow, "worker-1", kNow - absl::Minutes(1), 1);
  SweepReport first;
  ASSERT_TRUE(Sweep(*client_, policy_, kNow, first).ok());

  SweepReport again;
  const absl::Status swept = Sweep(*client_, policy_, kNow, again);
  ASSERT_TRUE(swept.ok()) << swept;
  EXPECT_EQ(again.settled(), 0);
}

}  // namespace
}  // namespace one_d4_worker
