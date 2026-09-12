#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/migration_files.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/poller_options.h"
#include "domains/games/apis/one_d4_worker/retention_policy.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

// What this worker shares with one_d4 and cannot see from its own side: the
// migration files that create the tables it polls, and the policy file both
// processes read.
//
// Column types are not checked here. The Postgres suites run the migration
// files themselves (migration_files), so a column that changed is a query
// that fails in the suite that reads it. What is left is whether every file
// runs at all, and what this worker does with the policy it loads.

TEST(SchemaContract, EveryMigrationFileIsReachableFromTheManifest) {
  const absl::StatusOr<std::vector<std::string>> steps = MigrationSteps();
  ASSERT_TRUE(steps.ok()) << steps.status();

  std::set<std::string> reachable = {absl::StrCat(MigrationsRoot(), "/manifest.txt")};
  for (const std::string& step : *steps) {
    const absl::StatusOr<std::string> path = MigrationSqlPath(step);
    if (path.ok()) reachable.insert(*path);
  }

  // Walks the runfiles copy of :migrations_sql only: a file on disk but
  // absent from that filegroup is invisible to every bazel test, which
  // migrations/README.md documents as this repo's standing trap.
  int seen = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(MigrationsRoot())) {
    if (!entry.is_regular_file()) continue;
    // lexically_relative, not relative: runfiles are symlinks, and relative()
    // canonicalizes through them to somewhere outside the tree.
    const std::string path = absl::StrCat(
        MigrationsRoot(), "/", entry.path().lexically_relative(MigrationsRoot()).generic_string());
    EXPECT_TRUE(reachable.count(path) == 1)
        << path << " is not reachable from manifest.txt — it never runs anywhere";
    ++seen;
  }
  // Exactly the manifest's steps plus the manifest itself. Pinned to the
  // manifest rather than a floor, so consolidating steps cannot trip it and a
  // walk that found nothing cannot pass it.
  EXPECT_EQ(seen, static_cast<int>(steps->size()) + 1)
      << "the runfiles tree and manifest.txt disagree about how many files there are";
}

// Every step resolves. A step whose file is missing from :migrations_sql is
// one the Java service dies on at boot and one no suite here can migrate.
TEST(SchemaContract, EveryManifestStepResolves) {
  const absl::StatusOr<std::vector<std::string>> steps = MigrationSteps();
  ASSERT_TRUE(steps.ok()) << steps.status();
  for (const std::string& step : *steps) {
    const absl::StatusOr<std::string> path = MigrationSqlPath(step);
    EXPECT_TRUE(path.ok()) << path.status();
  }
}

// The schema name is concatenated into the DDL that drops and recreates it,
// so it is refused before anything is sent — which is what makes this
// answerable without a server.
TEST(SchemaContract, ASchemaNameThatIsNotAPlainIdentifierIsRefused) {
  pg::Client unreachable("postgresql://127.0.0.1:1/nope?connect_timeout=2");
  EXPECT_EQ(ResetToMigratedSchema(unreachable, "one_d4_test; DROP SCHEMA public CASCADE").code(),
            absl::StatusCode::kInvalidArgument);
}

/// The shipped windows, loaded the way the worker loads them at startup.
///
/// Neither language holds a copy of the numbers — the worker reads the file at
/// startup, the Java service reads it off its classpath — so there is nothing
/// here to compare the two against each other. What is left to check is what
/// the worker does with what it read.
const RetentionPolicy& ShippedPolicy() {
  static const RetentionPolicy* const policy = [] {
    auto loaded = LoadRetentionPolicy("domains/games/apis/one_d4/retention_policy.json");
    EXPECT_TRUE(loaded.ok()) << loaded.status();
    return new RetentionPolicy(loaded.value_or(RetentionPolicy{}));
  }();
  return *policy;
}

/// The lease vocabulary production actually runs, taken through the same
/// function worker_main calls rather than through Poller::Options' defaults.
///
/// PollerOptionsFrom overwrites all three defaults, so asserting on them would
/// pin numbers no deployed worker reads, and would leave the mapping itself —
/// where the numbers can actually go wrong — unchecked. poller_test pins that
/// mapping against synthetic windows; this pins it against the file that
/// ships.
TEST(SchemaContract, ThePollerRunsTheShippedLeaseVocabulary) {
  const Poller::Options options = PollerOptionsFrom(ShippedPolicy(), "worker-1");
  EXPECT_EQ(options.lease, ShippedPolicy().lease);
  EXPECT_EQ(options.renew_every, ShippedPolicy().lease_renewal);
  EXPECT_EQ(options.max_run, ShippedPolicy().max_run);

  // The protocol invariant, restated against what the poller is handed rather
  // than against the file. LoadRetentionPolicy rejects a file that violates
  // it, so this holds unless the mapping loses it between the two.
  EXPECT_LE(options.renew_every * 4, options.lease);
}

}  // namespace
}  // namespace one_d4_worker
