#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H

#include <string>

#include "absl/status/statusor.h"

namespace one_d4_worker {

// The scratch database the Postgres suites here run against, and the one
// decision every one of them has to make the same way.

/// `$PG_TEST_DB_URL`, or:
///
/// - `Unavailable` when it is unset and `$CI` is not — a developer without a
///   database gets a skip.
/// - `FailedPrecondition` when it is unset and `$CI` is set. A skip reads as a
///   pass in a CI summary, and these suites are the only thing that exercises
///   the worker's half of the one_d4 schema (#1532), so a missing database is
///   a broken job rather than a broken developer setup.
///
/// `PgTestUrls.requireRawUrl` is the Java twin, on the same two variables.
/// Callers hold the distinction because GTEST_SKIP and ASSERT_ return from the
/// test body and cannot be delegated:
///
///     const absl::StatusOr<std::string> url = TestDbUrl();
///     if (absl::IsUnavailable(url.status())) GTEST_SKIP() << url.status().message();
///     ASSERT_TRUE(url.ok()) << url.status();
absl::StatusOr<std::string> TestDbUrl();

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_PG_TEST_DB_H
