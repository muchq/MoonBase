#include "domains/games/apis/one_d4_worker/pg_test_db.h"

#include <cstdlib>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace one_d4_worker {
namespace {

constexpr char kDbUrlEnv[] = "PG_TEST_DB_URL";
constexpr char kRequireEnv[] = "CI";

bool IsSet(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0';
}

}  // namespace

absl::StatusOr<std::string> TestDbUrl() {
  if (IsSet(kDbUrlEnv)) return std::string(std::getenv(kDbUrlEnv));
  if (IsSet(kRequireEnv)) {
    return absl::FailedPreconditionError(
        "PG_TEST_DB_URL is unset but CI is set. These suites are what exercises the schema;"
        " skipping them here would report a pass that ran no SQL.");
  }
  return absl::UnavailableError("PG_TEST_DB_URL unset");
}

}  // namespace one_d4_worker
