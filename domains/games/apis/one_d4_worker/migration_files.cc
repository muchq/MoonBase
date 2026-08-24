#include "domains/games/apis/one_d4_worker/migration_files.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace one_d4_worker {
namespace {

constexpr char kRoot[] = "domains/games/apis/one_d4/migrations";

absl::StatusOr<std::string> Read(const std::string& path) {
  std::ifstream file(path);
  if (!file.good()) return absl::NotFoundError(absl::StrCat("no migration file at ", path));
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

/// Schema names reach the DDL below by concatenation, so only the shape a
/// suite name has is accepted.
bool IsPlainIdentifier(const std::string& name) {
  if (name.empty() || absl::ascii_isdigit(name.front())) return false;
  for (const char c : name) {
    if (!absl::ascii_islower(c) && !absl::ascii_isdigit(c) && c != '_') return false;
  }
  return true;
}

}  // namespace

std::string MigrationsRoot() { return kRoot; }

absl::StatusOr<std::vector<std::string>> MigrationSteps() {
  const absl::StatusOr<std::string> manifest = Read(absl::StrCat(kRoot, "/manifest.txt"));
  if (!manifest.ok()) return manifest.status();

  std::istringstream lines(*manifest);
  std::vector<std::string> steps;
  std::string line;
  while (std::getline(lines, line)) {
    absl::StripAsciiWhitespace(&line);
    if (!line.empty() && line[0] != '#') steps.push_back(line);
  }
  if (steps.empty()) return absl::NotFoundError("manifest.txt names no steps");
  return steps;
}

absl::StatusOr<std::string> MigrationSqlPath(const std::string& step, const std::string& engine) {
  const std::string engine_path = absl::StrCat(kRoot, "/", engine, "/", step, ".sql");
  const std::string shared_path = absl::StrCat(kRoot, "/", step, ".sql");
  const bool engine_exists = std::filesystem::exists(engine_path);
  const bool shared_exists = std::filesystem::exists(shared_path);
  if (engine_exists && shared_exists) {
    return absl::FailedPreconditionError(absl::StrCat(step, " has both ", engine_path, " and ",
                                                      shared_path,
                                                      " — a forked step has no shared file"));
  }
  if (!engine_exists && !shared_exists) {
    return absl::NotFoundError(absl::StrCat(step, " has no SQL for ", engine, " — expected ",
                                            engine_path, " or ", shared_path));
  }
  return engine_exists ? engine_path : shared_path;
}

absl::Status ResetToMigratedSchema(pg::Client& client, const std::string& schema) {
  if (!IsPlainIdentifier(schema)) {
    return absl::InvalidArgumentError(absl::StrCat("not a plain schema name: ", schema));
  }
  if (const absl::Status dropped = client.ExecScript(
          absl::StrCat("DROP SCHEMA IF EXISTS ", schema, " CASCADE; CREATE SCHEMA ", schema, ";"));
      !dropped.ok()) {
    return dropped;
  }

  const absl::StatusOr<pg::Result> current = client.Exec("SELECT current_schema()");
  if (!current.ok()) return current.status();
  if (current->Get(0, 0) != schema) {
    return absl::FailedPreconditionError(
        absl::StrCat("unqualified names on this connection resolve to ",
                     current->Get(0, 0).value_or("(none)"), ", not ", schema));
  }

  const absl::StatusOr<std::vector<std::string>> steps = MigrationSteps();
  if (!steps.ok()) return steps.status();
  for (const std::string& step : *steps) {
    const absl::StatusOr<std::string> path = MigrationSqlPath(step, "pg");
    if (!path.ok()) return path.status();
    const absl::StatusOr<std::string> sql = Read(*path);
    if (!sql.ok()) return sql.status();
    if (const absl::Status applied = client.ExecScript(*sql); !applied.ok()) {
      return absl::Status(applied.code(),
                          absl::StrCat("migration step ", step, ": ", applied.message()));
    }
  }
  return absl::OkStatus();
}

}  // namespace one_d4_worker
