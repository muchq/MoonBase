#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"

namespace one_d4_worker {
namespace {

// The Java service owns `indexing_requests`; this worker is a second poller
// on it. Nothing but agreement makes that safe, and the agreement is
// invisible from either side alone: pg_queue_test builds its own copy of
// the table, so a column the Java migration changes would leave the C++
// tests green and production broken.
//
// The id column is why this exists. It is UUID, and a fixture that invents
// VARCHAR ids passes every test while telling you nothing.

std::string Read(const std::string& path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.good()) << "missing " << path;
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
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

/// The DDL for one table, as the dialect creates it and the migrations
/// then alter it.
std::map<std::string, std::string> JavaSchemaFor(const std::string& table) {
  std::map<std::string, std::string> columns =
      Columns(Read("domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
                   "PostgresSqlDialect.java"),
              table);
  for (const auto& [name, type] :
       Columns(Read("domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
                    "Migration.java"),
               table)) {
    columns[name] = type;
  }
  return columns;
}

std::map<std::string, std::string> JavaSchema() { return JavaSchemaFor("indexing_requests"); }

TEST(SchemaContract, TheJavaSchemaHasEveryColumnThisWorkerReadsOrWrites) {
  const std::map<std::string, std::string> java = JavaSchema();
  ASSERT_FALSE(java.empty()) << "read no columns at all — the DDL moved";

  for (const std::string& column :
       {"id", "player", "platform", "start_month", "end_month", "status", "created_at",
        "updated_at", "error_message", "games_indexed", "exclude_bullet", "skip_cache", "attempts",
        "owner_id", "lease_expires_at", "dedupe_key"}) {
    EXPECT_TRUE(java.count(column) == 1) << column << " is gone from the Java schema";
  }
}

TEST(SchemaContract, TheTestFixtureDeclaresTheSameTypes) {
  const std::map<std::string, std::string> java = JavaSchema();
  const std::map<std::string, std::string> fixture =
      Columns(Read("domains/games/apis/one_d4_worker/pg_queue_test.cc"), "indexing_requests");
  ASSERT_FALSE(fixture.empty()) << "read no columns from the fixture";

  for (const auto& [name, type] : fixture) {
    const auto declared = java.find(name);
    ASSERT_TRUE(declared != java.end()) << name << " is not in the Java schema";
    EXPECT_EQ(type, declared->second) << name << " is declared differently in the fixture";
  }
}

TEST(SchemaContract, IdIsAUuid) {
  // Named on its own because it is the one a text fixture gets wrong
  // silently: 'job-1' is a fine VARCHAR and not a UUID at all.
  EXPECT_EQ(JavaSchema()["id"], "UUID");
}

// The same argument, for the three tables the sink writes. Its fixture
// hand-copies their DDL too, so a column the Java migration changes leaves
// these tests green and production broken — and unlike indexing_requests,
// these are tables the C++ worker writes rows into rather than just claims
// from.

TEST(SchemaContract, TheJavaSchemaHasEveryColumnTheSinkWrites) {
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
    const std::map<std::string, std::string> java = JavaSchemaFor(table);
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
    const std::map<std::string, std::string> java = JavaSchemaFor(table);
    const std::map<std::string, std::string> fixture = Columns(fixture_source, table);
    ASSERT_FALSE(fixture.empty()) << "read no columns of " << table << " from the fixture";

    for (const auto& [name, type] : fixture) {
      const auto declared = java.find(name);
      ASSERT_TRUE(declared != java.end()) << name << " is not in the Java " << table;
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
  EXPECT_EQ(JavaSchemaFor("motif_occurrences")["id"], "VARCHAR(36)");
  EXPECT_EQ(JavaSchemaFor("game_features")["id"], "UUID");
}

TEST(SchemaContract, AFailedRunSaysWhatTheJavaWorkerSays) {
  // error_message is a column the API hands back, so what goes in it is a
  // contract with the caller and not a debugging aid. Both workers write
  // the same sentence, and neither writes the cause.
  const std::string java = Read(
      "domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/"
      "IndexWorker.java");
  const std::string poller = Read("domains/games/apis/one_d4_worker/poller.cc");

  const std::regex message(R"re("(Indexing failed[^"]*)")re");
  std::smatch found;
  ASSERT_TRUE(std::regex_search(java, found, message))
      << "IndexWorker no longer stores a fixed failure message";
  EXPECT_THAT(poller, testing::HasSubstr(absl::StrCat("\"", found[1].str(), "\"")))
      << "the C++ worker stores a different sentence than the Java one";
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
