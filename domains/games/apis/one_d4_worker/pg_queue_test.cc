#include "domains/games/apis/one_d4_worker/pg_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

// The queue's rules are the ones that let a worker that is not the Java one
// poll the same table: a claim is exclusive, every terminal write is fenced
// on ownership, and an expired lease is claimable by anyone. They are rules
// about concurrent SQL, so they are tested against a real Postgres — the
// CI job supplies one, and without PG_TEST_DB_URL these skip.

class PgQueueTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    client_ = std::make_unique<pg::Client>(url);

    // Column for column what PostgresSqlDialect creates and Migration adds,
    // types included — schema_contract_test is what keeps this copy honest.
    // The id being UUID rather than text is the reason that test exists: a
    // fixture that invents VARCHAR ids passes happily and tells you nothing
    // about production.
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS indexing_requests").ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE indexing_requests (
            id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            player         VARCHAR(255) NOT NULL,
            platform       VARCHAR(50) NOT NULL,
            start_month    VARCHAR(7) NOT NULL,
            end_month      VARCHAR(7) NOT NULL,
            status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
            created_at     TIMESTAMP NOT NULL DEFAULT now(),
            updated_at     TIMESTAMP NOT NULL DEFAULT now(),
            error_message  TEXT,
            games_indexed  INT DEFAULT 0,
            exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE,
            owner_id       VARCHAR(128),
            lease_expires_at TIMESTAMP,
            skip_cache     BOOLEAN DEFAULT FALSE,
            attempts       INT DEFAULT 0,
            dedupe_key     VARCHAR(600)
        ))")
                    .ok());
    queue_ = std::make_unique<PgQueue>(*client_);
  }

  /// A stable UUID per test-local name, because the real id column is one.
  static std::string Id(int n) { return absl::StrFormat("00000000-0000-4000-8000-%012d", n); }

  void Insert(const std::string& id, const std::string& player,
              const std::string& status = "PENDING") {
    ASSERT_TRUE(
        client_
            ->Exec("INSERT INTO indexing_requests (id, player, platform, start_month,"
                   " end_month, status, created_at, updated_at) VALUES ($1, $2, 'chess.com',"
                   " '2026-01', '2026-02', $3, NOW(), NOW())",
                   {id, player, status})
            .ok());
  }

  std::string Column(const std::string& id, const std::string& column) {
    const auto result = client_->Exec(
        absl::StrCat("SELECT ", column, " FROM indexing_requests WHERE id = $1"), {id});
    EXPECT_TRUE(result.ok()) << result.status();
    if (!result.ok() || result->rows() == 0) return "(none)";
    return result->Get(0, 0).value_or("(null)");
  }

  std::unique_ptr<pg::Client> client_;
  std::unique_ptr<PgQueue> queue_;
};

TEST_F(PgQueueTest, ClaimsNothingFromAnEmptyQueue) {
  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  EXPECT_FALSE(claimed->has_value());
}

TEST_F(PgQueueTest, ClaimsAPendingRequestAndReadsItBack) {
  Insert(Id(1), "hikaru");

  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  ASSERT_TRUE(claimed->has_value());
  EXPECT_EQ((*claimed)->id, Id(1));
  EXPECT_EQ((*claimed)->player, "hikaru");
  EXPECT_EQ((*claimed)->start_month, "2026-01");
  EXPECT_EQ((*claimed)->end_month, "2026-02");
  EXPECT_EQ((*claimed)->attempts, 1) << "claiming spends an attempt";
  EXPECT_EQ(Column(Id(1), "owner_id"), "worker-1");
}

TEST_F(PgQueueTest, TwoWorkersCannotClaimTheSameRequest) {
  // The rule the whole design rests on: any number of pollers, one owner.
  Insert(Id(1), "hikaru");

  const auto first = queue_->ClaimNext("worker-1", absl::Minutes(5));
  const auto second = queue_->ClaimNext("worker-2", absl::Minutes(5));
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_TRUE(first->has_value());
  EXPECT_FALSE(second->has_value());
}

TEST_F(PgQueueTest, ClaimsAgainOnceTheLeaseHasExpired) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Seconds(-1)).ok());

  const auto taken = queue_->ClaimNext("worker-2", absl::Minutes(5));
  ASSERT_TRUE(taken.ok()) << taken.status();
  ASSERT_TRUE(taken->has_value());
  EXPECT_EQ(Column(Id(1), "owner_id"), "worker-2");
  EXPECT_EQ((*taken)->attempts, 2) << "the second attempt at the same request";
}

TEST_F(PgQueueTest, HeartbeatHoldsTheLeaseAndSaysWhenItIsLost) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto held = queue_->Heartbeat(Id(1), "worker-1", absl::Minutes(5));
  ASSERT_TRUE(held.ok()) << held.status();
  EXPECT_TRUE(*held);

  const auto stolen = queue_->Heartbeat(Id(1), "worker-2", absl::Minutes(5));
  ASSERT_TRUE(stolen.ok()) << stolen.status();
  EXPECT_FALSE(*stolen);
}

TEST_F(PgQueueTest, CompleteIsFencedOnOwnership) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto stranger = queue_->Complete(Id(1), "worker-2", 99);
  ASSERT_TRUE(stranger.ok()) << stranger.status();
  EXPECT_FALSE(*stranger);
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING");

  const auto owner = queue_->Complete(Id(1), "worker-1", 42);
  ASSERT_TRUE(owner.ok()) << owner.status();
  EXPECT_TRUE(*owner);
  EXPECT_EQ(Column(Id(1), "status"), "COMPLETED");
  EXPECT_EQ(Column(Id(1), "games_indexed"), "42");
  EXPECT_EQ(Column(Id(1), "owner_id"), "(null)");
}

TEST_F(PgQueueTest, FailIsFencedOnOwnershipToo) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto stranger = queue_->Fail(Id(1), "worker-2", "not mine to fail");
  ASSERT_TRUE(stranger.ok()) << stranger.status();
  EXPECT_FALSE(*stranger);
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING");

  const auto owner = queue_->Fail(Id(1), "worker-1", "chess.com said no");
  ASSERT_TRUE(owner.ok()) << owner.status();
  EXPECT_TRUE(*owner);
  EXPECT_EQ(Column(Id(1), "status"), "FAILED");
  EXPECT_EQ(Column(Id(1), "error_message"), "chess.com said no");
}

TEST_F(PgQueueTest, WillNotClaimWorkThatIsAlreadyFinished) {
  Insert(Id(3), "hikaru", "COMPLETED");
  Insert(Id(4), "hikaru", "FAILED");

  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  EXPECT_FALSE(claimed->has_value());
}

TEST_F(PgQueueTest, TakesTheOldestRequestFirst) {
  Insert(Id(2), "second");
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET created_at = NOW() - INTERVAL '1 hour'"
                         " WHERE id = $1",
                         {Id(2)})
                  .ok());
  Insert(Id(1), "first");

  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  ASSERT_TRUE(claimed->has_value());
  EXPECT_EQ((*claimed)->id, Id(2));
}

TEST_F(PgQueueTest, StopsClaimingAfterTooManyAttempts) {
  // A range that fails every time is a request nobody should keep retrying.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET attempts = $1 WHERE id = $2",
                         {std::to_string(PgQueue::kMaxAttempts), Id(1)})
                  .ok());

  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  EXPECT_FALSE(claimed->has_value());
}

}  // namespace
}  // namespace one_d4_worker
