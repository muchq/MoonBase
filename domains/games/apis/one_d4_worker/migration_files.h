#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_MIGRATION_FILES_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_MIGRATION_FILES_H

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

// The one_d4 schema, read from the numbered .sql files under
// one_d4/migrations (#1419) — the same files the Java Migration and the
// one_d4_migrate deploy step run against production, reached through
// runfiles instead of a classpath. MigrationFiles.java is the twin, and the
// resolution rule below is its rule.
//
// Test-only: the worker polls these tables and never creates them. What it
// buys is that a suite's tables are the deployed tables, so a column a
// migration changes fails the suite that reads it instead of passing
// against a fixture's copy.

/// The directory the files are read from, relative to the runfiles root.
std::string MigrationsRoot();

/// The manifest's step names, in the order they run.
absl::StatusOr<std::vector<std::string>> MigrationSteps();

/// The file one step resolves to for one engine ("pg" or "h2"):
/// `<engine>/<step>.sql` when the engines fork, `<step>.sql` when they
/// agree. Both or neither is refused rather than guessed around.
absl::StatusOr<std::string> MigrationSqlPath(const std::string& step, const std::string& engine);

/// Drops `schema`, recreates it, and runs the Postgres migrations into it in
/// manifest order. `client` is the connection the caller goes on to test
/// against; it must resolve unqualified names to `schema`, and that is
/// checked rather than assumed — a suite whose search_path went missing
/// would otherwise build its tables in public alongside every sibling
/// suite's rows, and answer counting questions for all of them.
absl::Status ResetToMigratedSchema(pg::Client& client, const std::string& schema);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_MIGRATION_FILES_H
