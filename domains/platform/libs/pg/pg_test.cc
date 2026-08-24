#include "domains/platform/libs/pg/pg.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

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

class PgTransactionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    url_ = std::getenv("PG_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    client_ = std::make_unique<pg::Client>(url_);
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS pg_txn_test").ok());
    ASSERT_TRUE(client_->Exec("CREATE TABLE pg_txn_test (n integer)").ok());
  }

  int Count() {
    auto result = client_->Exec("SELECT 1 FROM pg_txn_test");
    EXPECT_TRUE(result.ok()) << result.status();
    return result.ok() ? result->rows() : -1;
  }

  const char* url_ = nullptr;
  std::unique_ptr<pg::Client> client_;
};

TEST_F(PgTransactionTest, CommitsWhenTheBodySucceeds) {
  const absl::Status status = client_->InTransaction([](pg::Transaction& txn) -> absl::Status {
    if (auto first = txn.Exec("INSERT INTO pg_txn_test VALUES (1)"); !first.ok()) {
      return first.status();
    }
    return txn.Exec("INSERT INTO pg_txn_test VALUES (2)").status();
  });
  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(Count(), 2);
}

TEST_F(PgTransactionTest, RollsBackWhenTheBodyReturnsAnError) {
  const absl::Status status = client_->InTransaction([](pg::Transaction& txn) -> absl::Status {
    if (auto wrote = txn.Exec("INSERT INTO pg_txn_test VALUES (1)"); !wrote.ok()) {
      return wrote.status();
    }
    // The body changed its mind after writing — nothing may survive.
    return absl::AbortedError("caller declined to commit");
  });
  EXPECT_EQ(status.code(), absl::StatusCode::kAborted);
  EXPECT_EQ(Count(), 0);

  // A failing statement rolls back just the same, and leaves the client
  // usable rather than stuck in an aborted transaction.
  const absl::Status failed = client_->InTransaction([](pg::Transaction& txn) -> absl::Status {
    if (auto wrote = txn.Exec("INSERT INTO pg_txn_test VALUES (3)"); !wrote.ok()) {
      return wrote.status();
    }
    return txn.Exec("SELECT no_such_function()").status();
  });
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(Count(), 0);
  EXPECT_TRUE(client_->Exec("SELECT 1").ok()) << "the connection must still be usable";
}

TEST_F(PgTransactionTest, CannotCommitAfterAnIgnoredStatementFailure) {
  const absl::Status status = client_->InTransaction([](pg::Transaction& txn) -> absl::Status {
    auto wrote = txn.Exec("INSERT INTO pg_txn_test VALUES (1)");
    if (!wrote.ok()) return wrote.status();
    // A callback can accidentally ignore a failed statement. PostgreSQL
    // answers the later COMMIT with command tag ROLLBACK, which still has
    // PGRES_COMMAND_OK; InTransaction must remember the earlier failure.
    txn.Exec("SELECT no_such_function()").IgnoreError();
    return absl::OkStatus();
  });

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(Count(), 0);
  EXPECT_TRUE(client_->Exec("SELECT 1").ok()) << "the connection must still be usable";
}

// The property the chat append depends on: a statement issued after the
// transaction's lock is held takes a fresh snapshot, so it sees rows
// another writer committed while this one was waiting. A CTE-chained
// single statement would still be reading its pre-wait snapshot here.
TEST_F(PgTransactionTest, StatementsAfterALockSeeConcurrentCommits) {
  ASSERT_TRUE(
      client_->Exec("CREATE TABLE IF NOT EXISTS pg_txn_lock (id integer PRIMARY KEY)").ok());
  ASSERT_TRUE(client_->Exec("TRUNCATE pg_txn_lock").ok());
  ASSERT_TRUE(client_->Exec("INSERT INTO pg_txn_lock VALUES (1)").ok());

  // Holder takes the lock and inserts a row, uncommitted.
  pg::Client holder(url_);
  ASSERT_TRUE(holder.Exec("BEGIN").ok());
  ASSERT_TRUE(holder.Exec("SELECT 1 FROM pg_txn_lock WHERE id = 1 FOR UPDATE").ok());
  ASSERT_TRUE(holder.Exec("INSERT INTO pg_txn_test VALUES (99)").ok());

  pg::Client waiter(url_);
  int seen = -1;
  std::thread thread([&] {
    const absl::Status status = waiter.InTransaction([&](pg::Transaction& txn) -> absl::Status {
      auto locked = txn.Exec("SELECT 1 FROM pg_txn_lock WHERE id = 1 FOR UPDATE");
      if (!locked.ok()) return locked.status();
      auto rows = txn.Exec("SELECT 1 FROM pg_txn_test");
      if (!rows.ok()) return rows.status();
      seen = rows->rows();
      return absl::OkStatus();
    });
    EXPECT_TRUE(status.ok()) << status;
  });

  ASSERT_TRUE(holder.Exec("COMMIT").ok());
  thread.join();
  EXPECT_EQ(seen, 1) << "the post-lock read must see the row committed while it waited";
}

// Exclusive ownership: the connection is held for the whole callback,
// so another caller's statement cannot land in the middle of one.
TEST_F(PgTransactionTest, OtherCallersCannotInterleaveOnTheSameConnection) {
  std::mutex mu;
  std::condition_variable cv;
  bool inside = false;
  bool outsider_finished = false;

  std::thread outsider;
  const absl::Status status = client_->InTransaction([&](pg::Transaction& txn) -> absl::Status {
    if (auto wrote = txn.Exec("INSERT INTO pg_txn_test VALUES (1)"); !wrote.ok()) {
      return wrote.status();
    }
    outsider = std::thread([&] {
      EXPECT_TRUE(client_->Exec("INSERT INTO pg_txn_test VALUES (2)").ok());
      const std::lock_guard<std::mutex> lock(mu);
      outsider_finished = true;
      cv.notify_all();
    });
    {
      std::unique_lock<std::mutex> lock(mu);
      inside = true;
      // Bounded: if the outsider ever did slip in, this returns early
      // and the assertion below fails instead of hanging the suite.
      cv.wait_for(lock, std::chrono::seconds(2), [&] { return outsider_finished; });
      EXPECT_FALSE(outsider_finished) << "an outside Exec ran inside the transaction";
    }
    return absl::OkStatus();
  });

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_TRUE(inside);
  outsider.join();
  EXPECT_EQ(Count(), 2) << "the outsider's write lands once the transaction releases";
}

// The .sql files one_d4's migrations ship as, run the way psql runs them.
class PgScriptTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    client_ = std::make_unique<pg::Client>(url);
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS pg_script_test").ok());
  }

  std::unique_ptr<pg::Client> client_;
};

// Why this is a second method rather than a use of Exec: Exec binds
// parameters, which puts it on the extended protocol, and that protocol
// carries one statement per message however few parameters are passed.
TEST_F(PgScriptTest, ExecRefusesTheScriptExecScriptRuns) {
  const std::string script =
      "CREATE TABLE pg_script_test (n integer); INSERT INTO pg_script_test VALUES (1);";
  EXPECT_FALSE(client_->Exec(script).ok());
  EXPECT_TRUE(client_->ExecScript(script).ok());
}

TEST_F(PgScriptTest, RunsEveryStatementInTheScript) {
  ASSERT_TRUE(client_
                  ->ExecScript(R"(
      CREATE TABLE pg_script_test (n integer);
      INSERT INTO pg_script_test VALUES (1);
      INSERT INTO pg_script_test VALUES (2);
  )")
                  .ok());

  const auto rows = client_->Exec("SELECT n FROM pg_script_test ORDER BY n");
  ASSERT_TRUE(rows.ok()) << rows.status();
  EXPECT_EQ(rows->rows(), 2);
}

// Statement-at-a-time is what Exec offers, and a script is not that: the
// implicit transaction Postgres wraps around a multi-statement script means
// the earlier statements do not survive a later failure.
TEST_F(PgScriptTest, LeavesNothingBehindWhenAStatementFails) {
  const absl::Status status = client_->ExecScript(R"(
      CREATE TABLE pg_script_test (n integer);
      INSERT INTO pg_script_test VALUES (1);
      INSERT INTO pg_script_test VALUES ('not a number');
  )");
  EXPECT_FALSE(status.ok());

  const auto exists = client_->Exec("SELECT to_regclass('pg_script_test')");
  ASSERT_TRUE(exists.ok()) << exists.status();
  EXPECT_EQ(exists->Get(0, 0), std::nullopt) << "the failed script left its table behind";
}

// Dollar-quoted bodies carry semicolons, so a script split on ';' by hand
// would send half a DO block. libpq is what parses this one.
TEST_F(PgScriptTest, RunsADollarQuotedBlockWholeAndItsErrorsAreHandled) {
  ASSERT_TRUE(client_
                  ->ExecScript(R"(
      CREATE TABLE pg_script_test (n integer);
      DO $$ BEGIN
        ALTER TABLE pg_script_test ADD CONSTRAINT pg_script_test_n_unique UNIQUE (n);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$;
      DO $$ BEGIN
        ALTER TABLE pg_script_test ADD CONSTRAINT pg_script_test_n_unique UNIQUE (n);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$;
  )")
                  .ok());

  const auto constraints =
      client_->Exec("SELECT conname FROM pg_constraint WHERE conname = 'pg_script_test_n_unique'");
  ASSERT_TRUE(constraints.ok()) << constraints.status();
  EXPECT_EQ(constraints->rows(), 1);
}

TEST_F(PgScriptTest, RefusesAScriptWithNoStatementsInIt) {
  // What an empty or all-comments file reaches here as. Reporting success
  // would make a migration that never ran indistinguishable from one that did.
  EXPECT_EQ(client_->ExecScript("-- nothing to do\n").code(), absl::StatusCode::kInvalidArgument);
}

TEST(PgScriptOfflineTest, ExecScriptOnUnreachableServerReturnsUnavailable) {
  pg::Client client("postgresql://127.0.0.1:1/nope?connect_timeout=2");
  EXPECT_EQ(client.ExecScript("SELECT 1; SELECT 2;").code(), absl::StatusCode::kUnavailable);
}

}  // namespace
