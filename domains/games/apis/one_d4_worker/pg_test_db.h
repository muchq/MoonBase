#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H

#include <string>

#include "absl/status/statusor.h"

namespace one_d4_worker {

/// `$PG_TEST_DB_URL`; `Unavailable` when it is unset, or `FailedPrecondition`
/// when `$CI` is also set — under CI a skip would report a pass that ran no
/// SQL. `PgTestUrls.requireRawUrl` is the Java twin. Callers branch because
/// GTEST_SKIP returns from the test body and cannot be delegated:
///
///     const absl::StatusOr<std::string> url = TestDbUrl();
///     if (absl::IsUnavailable(url.status())) GTEST_SKIP() << url.status().message();
///     ASSERT_TRUE(url.ok()) << url.status();
absl::StatusOr<std::string> TestDbUrl();

/// The same decision over explicit values, so the three outcomes are testable
/// without touching the environment. Null or empty means unset.
absl::StatusOr<std::string> TestDbUrlFrom(const char* url, const char* required);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H
