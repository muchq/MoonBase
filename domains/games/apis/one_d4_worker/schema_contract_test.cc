#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"

namespace one_d4_worker {
namespace {

// The schema lives in one_d4's migrations/ .sql files (#1419); this worker
// is a second poller on the tables they create. Nothing but agreement makes
// that safe, and the agreement is invisible from either side alone:
// pg_queue_test builds its own copy of the table, so a column a migration
// changes would leave the C++ tests green and production broken.
//
// The id column is why this exists. It is UUID, and a fixture that invents
// VARCHAR ids passes every test while telling you nothing.

constexpr char kMigrationsDir[] = "domains/games/apis/one_d4/migrations";

std::string Read(const std::string& path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.good()) << "missing " << path;
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

/// manifest.txt's step names, in order — the same list Migration runs.
std::vector<std::string> ManifestSteps() {
  std::istringstream lines(Read(absl::StrCat(kMigrationsDir, "/manifest.txt")));
  std::vector<std::string> steps;
  std::string line;
  while (std::getline(lines, line)) {
    absl::StripAsciiWhitespace(&line);
    if (!line.empty() && line[0] != '#') steps.push_back(line);
  }
  return steps;
}

/// One step's file for one engine, resolved the way Migration resolves it:
/// the engine directory when the engines fork, the shared file when they
/// agree — and never both, which would be two sources of truth.
std::string ResolveStep(const std::string& step, const std::string& engine) {
  const std::string engine_path = absl::StrCat(kMigrationsDir, "/", engine, "/", step, ".sql");
  const std::string shared_path = absl::StrCat(kMigrationsDir, "/", step, ".sql");
  const bool engine_exists = std::filesystem::exists(engine_path);
  const bool shared_exists = std::filesystem::exists(shared_path);
  EXPECT_FALSE(engine_exists && shared_exists)
      << step << " has both an " << engine << " file and a shared file";
  EXPECT_TRUE(engine_exists || shared_exists) << step << " has no file for " << engine;
  return engine_exists ? engine_path : shared_path;
}

/// Every statement the Postgres migration runs, concatenated in order.
const std::string& PostgresMigrationSql() {
  static const std::string* const sql = [] {
    auto* all = new std::string;
    for (const std::string& step : ManifestSteps()) {
      absl::StrAppend(all, Read(ResolveStep(step, "pg")), "\n");
    }
    return all;
  }();
  return *sql;
}

/// column name -> declared type, from a CREATE TABLE body plus any ALTER
/// TABLE ... ADD COLUMN for the same table.
std::map<std::string, std::string> Columns(const std::string& source, const std::string& table) {
  std::map<std::string, std::string> columns;

  const std::regex create(
      absl::StrCat("CREATE TABLE (IF NOT EXISTS )?", table, R"(\s*\(([\s\S]*?)\n\s*\))"));
  std::smatch body;
  if (std::regex_search(source, body, create)) {
    std::istringstream lines(body[2].str());
    std::string line;
    while (std::getline(lines, line)) {
      const std::regex column(R"(^\s*([a-z_]+)\s+([A-Za-z]+(\(\d+\))?))");
      std::smatch parsed;
      if (std::regex_search(line, parsed, column)) columns[parsed[1]] = parsed[2];
    }
  }

  const std::regex added(absl::StrCat(
      "ALTER TABLE ", table, R"( ADD COLUMN IF NOT EXISTS ([a-z_]+) ([A-Za-z]+(\(\d+\))?))"));
  for (std::sregex_iterator it(source.begin(), source.end(), added), end; it != end; ++it) {
    columns[(*it)[1]] = (*it)[2];
  }
  return columns;
}

/// The DDL for one table, as the migrations create and then alter it.
std::map<std::string, std::string> MigrationSchemaFor(const std::string& table) {
  return Columns(PostgresMigrationSql(), table);
}

std::map<std::string, std::string> MigrationSchema() {
  return MigrationSchemaFor("indexing_requests");
}

// The migration files are read blind off the manifest, so an unreachable
// file — in :migrations_sql but missing from the manifest, or shadowed by
// the resolution rule — is one this test silently never scrapes and
// production never runs. Walks the runfiles copy of :migrations_sql only:
// a file on disk but absent from that filegroup (or from BUILD entirely)
// is invisible to every bazel test, which migrations/README.md documents
// as this repo's standing trap.
TEST(SchemaContract, EveryMigrationFileIsReachableFromTheManifest) {
  std::set<std::string> reachable = {"manifest.txt"};
  for (const std::string& step : ManifestSteps()) {
    for (const std::string& engine : {"pg", "h2"}) {
      if (std::filesystem::exists(absl::StrCat(kMigrationsDir, "/", engine, "/", step, ".sql"))) {
        reachable.insert(absl::StrCat(engine, "/", step, ".sql"));
      }
    }
    if (std::filesystem::exists(absl::StrCat(kMigrationsDir, "/", step, ".sql"))) {
      reachable.insert(absl::StrCat(step, ".sql"));
    }
  }
  int seen = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(kMigrationsDir)) {
    if (!entry.is_regular_file()) continue;
    // lexically_relative, not relative: runfiles are symlinks, and relative()
    // canonicalizes through them to somewhere outside the tree.
    const std::string relative = entry.path().lexically_relative(kMigrationsDir).generic_string();
    EXPECT_TRUE(reachable.count(relative) == 1)
        << relative << " is not reachable from manifest.txt — it never runs anywhere";
    ++seen;
  }
  // A loose floor: only there to catch the walk finding nothing at all, so a
  // legitimate consolidation of steps doesn't trip it.
  EXPECT_GT(seen, 10) << "the directory walk found almost nothing — the data moved";
}

// Both engines resolve every step, checked here as well as in
// MigrationFilesTest because this test's view of the schema is built from
// the same resolution rule: a step it cannot resolve is a step it is not
// checking the fixtures against.
TEST(SchemaContract, EveryManifestStepResolvesForBothEngines) {
  const std::vector<std::string> steps = ManifestSteps();
  ASSERT_FALSE(steps.empty()) << "read no steps at all — the manifest moved";
  for (const std::string& step : steps) {
    for (const std::string& engine : {"pg", "h2"}) {
      ResolveStep(step, engine);  // EXPECTs inside
    }
  }
}

TEST(SchemaContract, TheMigrationSchemaHasEveryColumnThisWorkerReadsOrWrites) {
  const std::map<std::string, std::string> java = MigrationSchema();
  ASSERT_FALSE(java.empty()) << "read no columns at all — the DDL moved";

  for (const std::string& column :
       {"id", "player", "platform", "start_month", "end_month", "status", "created_at",
        "updated_at", "error_message", "games_indexed", "exclude_bullet", "skip_cache", "attempts",
        "owner_id", "lease_expires_at", "dedupe_key"}) {
    EXPECT_TRUE(java.count(column) == 1) << column << " is gone from the migration schema";
  }
}

TEST(SchemaContract, TheTestFixtureDeclaresTheSameTypes) {
  const std::map<std::string, std::string> java = MigrationSchema();
  const std::map<std::string, std::string> fixture =
      Columns(Read("domains/games/apis/one_d4_worker/pg_queue_test.cc"), "indexing_requests");
  ASSERT_FALSE(fixture.empty()) << "read no columns from the fixture";

  for (const auto& [name, type] : fixture) {
    const auto declared = java.find(name);
    ASSERT_TRUE(declared != java.end()) << name << " is not in the migration schema";
    EXPECT_EQ(type, declared->second) << name << " is declared differently in the fixture";
  }
}

TEST(SchemaContract, IdIsAUuid) {
  // Named on its own because it is the one a text fixture gets wrong
  // silently: 'job-1' is a fine VARCHAR and not a UUID at all.
  EXPECT_EQ(MigrationSchema()["id"], "UUID");
}

// The same argument, for the three tables the sink writes. Its fixture
// hand-copies their DDL too, so a column a migration changes leaves these
// tests green and production broken — and unlike indexing_requests, these
// are tables the C++ worker writes rows into rather than just claims from.

TEST(SchemaContract, TheMigrationSchemaHasEveryColumnTheSinkWrites) {
  for (const auto& [table, wanted] : std::vector<std::pair<std::string, std::vector<std::string>>>{
           {"game_features",
            {"id", "request_id", "game_url", "platform", "white_username", "black_username",
             "white_elo", "black_elo", "white_title", "black_title", "time_class", "eco",
             "opening_name", "opening_family", "result", "played_at", "num_moves", "indexed_at",
             "pgn"}},
           {"motif_occurrences",
            {"id", "game_url", "motif", "ply", "side", "move_number", "description", "moved_piece",
             "attacker", "target", "is_discovered", "is_mate", "pin_type"}},
           {"indexed_periods",
            {"id", "player", "platform", "year_month", "fetched_at", "is_complete", "games_count",
             "exclude_bullet"}}}) {
    const std::map<std::string, std::string> java = MigrationSchemaFor(table);
    ASSERT_FALSE(java.empty()) << "read no columns of " << table << " — the DDL moved";
    for (const std::string& column : wanted) {
      EXPECT_TRUE(java.count(column) == 1) << column << " is gone from " << table;
    }
  }
}

TEST(SchemaContract, TheSinkFixtureDeclaresTheSameTypes) {
  const std::string fixture_source = Read("domains/games/apis/one_d4_worker/pg_game_sink_test.cc");
  int checked = 0;
  for (const std::string& table : {"game_features", "motif_occurrences", "indexed_periods"}) {
    const std::map<std::string, std::string> java = MigrationSchemaFor(table);
    const std::map<std::string, std::string> fixture = Columns(fixture_source, table);
    ASSERT_FALSE(fixture.empty()) << "read no columns of " << table << " from the fixture";

    for (const auto& [name, type] : fixture) {
      const auto declared = java.find(name);
      ASSERT_TRUE(declared != java.end()) << name << " is not in the migration " << table;
      EXPECT_EQ(type, declared->second)
          << name << " is declared differently in the " << table << " fixture";
      ++checked;
    }
  }
  EXPECT_GT(checked, 30) << "the fixture parse found almost nothing to compare";
}

TEST(SchemaContract, TheOccurrenceIdIsNotAUuidColumn) {
  // motif_occurrences.id is a VARCHAR holding a UUID, unlike every other
  // id here — which is why the sink generates it with gen_random_uuid()
  // cast to text. A fixture that made it UUID would accept a cast the real
  // column rejects.
  EXPECT_EQ(MigrationSchemaFor("motif_occurrences")["id"], "VARCHAR(36)");
  EXPECT_EQ(MigrationSchemaFor("game_features")["id"], "UUID");
}

// The reanalysis fixture is a third hand-copy of the two tables the pass
// reads and writes, so it drifts on the same terms as the sink's.
TEST(SchemaContract, TheReanalysisFixtureDeclaresTheSameTypes) {
  const std::string fixture_source = Read("domains/games/apis/one_d4_worker/pg_reanalysis_test.cc");
  int checked = 0;
  for (const std::string& table : {"game_features", "motif_occurrences"}) {
    const std::map<std::string, std::string> java = MigrationSchemaFor(table);
    const std::map<std::string, std::string> fixture = Columns(fixture_source, table);
    ASSERT_FALSE(fixture.empty()) << "read no columns of " << table << " from the fixture";

    for (const auto& [name, type] : fixture) {
      const auto declared = java.find(name);
      ASSERT_TRUE(declared != java.end()) << name << " is not in the migration " << table;
      EXPECT_EQ(type, declared->second)
          << name << " is declared differently in the reanalysis " << table << " fixture";
      ++checked;
    }
  }
  EXPECT_GT(checked, 12) << "the fixture parse found almost nothing to compare";
}

// reanalysis_requests is not a shared table — the indexers never touch it,
// which is the point of it existing (#1389 phase 5). But the migrations
// still own its DDL and this worker still hand-copies it into a fixture, so
// the same drift is available: a column renamed in pg/V017 leaves
// reanalysis_queue_test green against a table production does not have.

TEST(SchemaContract, TheMigrationSchemaHasEveryColumnTheReanalysisQueueTouches) {
  const std::map<std::string, std::string> java = MigrationSchemaFor("reanalysis_requests");
  ASSERT_FALSE(java.empty()) << "read no columns at all — the DDL moved";

  for (const std::string& column :
       {"id", "status", "created_at", "updated_at", "owner_id", "lease_expires_at", "attempts",
        "error_message", "cursor_game_url", "games_processed", "games_failed"}) {
    EXPECT_TRUE(java.count(column) == 1) << column << " is gone from reanalysis_requests";
  }
}

TEST(SchemaContract, TheReanalysisRequestFixtureDeclaresTheSameTypes) {
  const std::map<std::string, std::string> java = MigrationSchemaFor("reanalysis_requests");
  // Both copies. pg_reanalysis_test carries one too, and it is the one
  // behind the fence — a lease_expires_at that drifted to TIMESTAMPTZ
  // there would compare against NOW() differently than production does.
  int checked = 0;
  for (const std::string& path : {"domains/games/apis/one_d4_worker/reanalysis_queue_test.cc",
                                  "domains/games/apis/one_d4_worker/pg_reanalysis_test.cc"}) {
    const std::map<std::string, std::string> fixture = Columns(Read(path), "reanalysis_requests");
    ASSERT_FALSE(fixture.empty()) << "read no columns from " << path;

    for (const auto& [name, type] : fixture) {
      const auto declared = java.find(name);
      ASSERT_TRUE(declared != java.end()) << name << " is not in the migration schema";
      EXPECT_EQ(type, declared->second) << name << " is declared differently in " << path;
      ++checked;
    }
  }
  EXPECT_GT(checked, 20) << "one of the two fixtures was not parsed";
}

TEST(SchemaContract, TheReanalysisIdIsAUuidToo) {
  EXPECT_EQ(MigrationSchemaFor("reanalysis_requests")["id"], "UUID");
}

TEST(SchemaContract, ThePollerDefaultsMatchTheRetentionPolicy) {
  // The lease vocabulary is the Java service's: RetentionPolicy.LEASE is
  // what reclaimStale compares lease_expires_at against, and MAX_RUN is
  // the ceiling both sides agree makes a run a fault. The renewal interval
  // must leave several losable renewals inside one lease, or every lease
  // lapses between beats and healthy workers lose their ranges.
  const Poller::Options defaults;
  EXPECT_EQ(defaults.lease, absl::Minutes(5));
  EXPECT_EQ(defaults.max_run, absl::Hours(6));
  EXPECT_LE(defaults.renew_every * 4, defaults.lease);

  const std::string policy = Read(
      "domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
      "RetentionPolicy.java");
  EXPECT_THAT(policy, testing::HasSubstr("LEASE = Duration.ofMinutes(5)"));
  EXPECT_THAT(policy, testing::HasSubstr("MAX_RUN = Duration.ofHours(6)"));
}

TEST(SchemaContract, BothQueuesShareOneAttemptBudget) {
  // The reanalysis header claims the same budget "for the same reason";
  // this is what makes that a fact rather than a sentence.
  EXPECT_EQ(PgReanalysisQueue::kMaxAttempts, PgQueue::kMaxAttempts);
}

TEST(SchemaContract, AFailedRunSaysAFixedSentenceAndNotTheCause) {
  // error_message is a column the API hands back, so what goes in it is a
  // contract with the caller and not a debugging aid: one fixed sentence,
  // never the cause. Stored rows already carry this exact string, so a
  // caller that matches on it must keep matching.
  const std::string poller = Read("domains/games/apis/one_d4_worker/poller.cc");
  EXPECT_THAT(poller, testing::HasSubstr("\"Indexing failed due to an internal error\""))
      << "the worker stores a different sentence than the API's callers have seen";
}

TEST(SchemaContract, AttemptsAgreeWithTheJavaLimit) {
  // Two workers with different ideas of when to give up would retry a
  // poisoned request forever, or abandon a healthy one early.
  const std::string source = Read(
      "domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
      "IndexingRequestStore.java");
  EXPECT_THAT(source, testing::HasSubstr(absl::StrCat("MAX_ATTEMPTS = ", PgQueue::kMaxAttempts)));
}

}  // namespace
}  // namespace one_d4_worker
