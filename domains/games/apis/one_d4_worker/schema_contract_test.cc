#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
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

std::map<std::string, std::string> JavaSchema() {
  std::map<std::string, std::string> columns =
      Columns(Read("domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
                   "PostgresSqlDialect.java"),
              "indexing_requests");
  for (const auto& [name, type] :
       Columns(Read("domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/"
                    "Migration.java"),
               "indexing_requests")) {
    columns[name] = type;
  }
  return columns;
}

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
