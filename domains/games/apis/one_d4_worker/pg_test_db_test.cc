#include "domains/games/apis/one_d4_worker/pg_test_db.h"

#include <gtest/gtest.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace one_d4_worker {
namespace {

// The gate the six Postgres suites here go through. Driven directly because
// every one of them branches on the status rather than asserting it: deleting
// the CI arm would turn all six green-by-skip and nothing else would notice.
// PgTestUrlsTest is the Java twin, on the same two variables.

TEST(PgTestDb, AConfiguredUrlIsReturned) {
  const absl::StatusOr<std::string> url = TestDbUrlFrom("postgresql://h/db", "1");
  ASSERT_TRUE(url.ok()) << url.status();
  EXPECT_EQ(*url, "postgresql://h/db");
}

TEST(PgTestDb, NoUrlAndNoCiSkips) {
  EXPECT_EQ(TestDbUrlFrom(nullptr, nullptr).status().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(TestDbUrlFrom("", "").status().code(), absl::StatusCode::kUnavailable);
}

TEST(PgTestDb, NoUrlUnderCiFails) {
  const absl::Status failed = TestDbUrlFrom(nullptr, "true").status();
  EXPECT_EQ(failed.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(failed.message().find("PG_TEST_DB_URL"), std::string_view::npos);
  EXPECT_NE(failed.message().find("CI"), std::string_view::npos);
}

}  // namespace
}  // namespace one_d4_worker
