#include "domains/games/apis/one_d4_worker/pg_test_db.h"

#include <cstdlib>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace one_d4_worker {
namespace {

constexpr char kDbUrlEnv[] = "PG_TEST_DB_URL";
constexpr char kRequireEnv[] = "CI";

bool IsSet(const char* value) { return value != nullptr && *value != '\0'; }

}  // namespace

absl::StatusOr<std::string> TestDbUrl() {
  return TestDbUrlFrom(std::getenv(kDbUrlEnv), std::getenv(kRequireEnv));
}

absl::StatusOr<std::string> TestDbUrlFrom(const char* url, const char* required) {
  if (IsSet(url)) return std::string(url);
  if (IsSet(required)) {
    return absl::FailedPreconditionError(
        "PG_TEST_DB_URL is unset but CI is set. These suites are what exercises the schema;"
        " skipping them here would report a pass that ran no SQL.");
  }
  return absl::UnavailableError("PG_TEST_DB_URL unset");
}

}  // namespace one_d4_worker
