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

// The header's healing promise, held to deterministically: killing our own
// backend is indistinguishable from a postgres restart as far as the
// client can tell, and pg_terminate_backend needs no superuser for the
// caller's own connection.
TEST(PgClientTest, ExecHealsAfterConnectionIsKilled) {
  const char* url = std::getenv("PG_TEST_DB_URL");
  if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";

  pg::Client client(url);
  const auto before = client.Exec("SELECT pg_backend_pid()");
  ASSERT_TRUE(before.ok());

  // The kill fails even after Exec's internal retry — each attempt
  // terminates the very connection it runs on.
  EXPECT_FALSE(client.Exec("SELECT pg_terminate_backend(pg_backend_pid())").ok());

  const auto after = client.Exec("SELECT pg_backend_pid()");
  ASSERT_TRUE(after.ok());
  EXPECT_NE(after->Get(0, 0), before->Get(0, 0));
}

}  // namespace
