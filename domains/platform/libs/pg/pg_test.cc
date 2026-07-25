#include "domains/platform/libs/pg/pg.h"

#include <cstdlib>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace {

// The DB-free contract tests. Full round-trips run in pg_ticket_vault_test
// (and any future consumer's suite) against a real postgres when
// *_TEST_DB_URL is set — libpq semantics aren't worth mocking.

TEST(PgClientTest, ExecOnUnreachableServerReturnsUnavailable) {
  // Port 1 refuses immediately; connect_timeout caps the pathological case.
  pg::Client client("postgresql://127.0.0.1:1/nope?connect_timeout=2");
  const auto result = client.Exec("SELECT 1");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnavailable);
}

}  // namespace
