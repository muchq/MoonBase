#include "domains/games/apis/one_d4_worker/pg_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "domains/games/apis/one_d4_worker/migration_files.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

// The queue's rules are the ones that let a worker that is not the Java one
// poll the same table: a claim is exclusive, every terminal write is fenced
// on ownership, and an expired lease is claimable by anyone. They are rules
// about concurrent SQL, so they are tested against a real Postgres — the
// CI job supplies one, and without PG_TEST_DB_URL these skip.

/// The budget this fixture runs the queue with. A number of its own rather
/// than the shipped one: these tests pin what happens when the budget is
/// reached, not what the deployment sets it to.
constexpr int kMaxAttempts = 3;

/// A schema of this suite's own. Every suite here gets the one database CI
/// runs and bazel runs them in parallel, so the tables are shared mutable
/// state otherwise.
constexpr char kSchema[] = "one_d4_pg_queue_test";

/// search_path on the connection rather than a qualified name on every
/// statement: PgQueue's SQL is the production SQL, and production does not
/// qualify.
std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema);
}

class PgQueueTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    conninfo_ = Conninfo(url);
    client_ = std::make_unique<pg::Client>(conninfo_);
    ASSERT_TRUE(ResetToMigratedSchema(*client_, kSchema).ok());
    queue_ = std::make_unique<PgQueue>(*client_, kMaxAttempts);
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

  std::string conninfo_;
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

  const auto held = queue_->Heartbeat({.id = Id(1), .owner = "worker-1"}, absl::Minutes(5));
  ASSERT_TRUE(held.ok()) << held.status();
  EXPECT_TRUE(*held);

  const auto stolen = queue_->Heartbeat({.id = Id(1), .owner = "worker-2"}, absl::Minutes(5));
  ASSERT_TRUE(stolen.ok()) << stolen.status();
  EXPECT_FALSE(*stolen);
}

TEST_F(PgQueueTest, CompleteIsFencedOnOwnership) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto stranger = queue_->Complete({.id = Id(1), .owner = "worker-2"}, 99);
  ASSERT_TRUE(stranger.ok()) << stranger.status();
  EXPECT_FALSE(*stranger);
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING");

  const auto owner = queue_->Complete({.id = Id(1), .owner = "worker-1"}, 42);
  ASSERT_TRUE(owner.ok()) << owner.status();
  EXPECT_TRUE(*owner);
  EXPECT_EQ(Column(Id(1), "status"), "COMPLETED");
  EXPECT_EQ(Column(Id(1), "games_indexed"), "42");
  EXPECT_EQ(Column(Id(1), "owner_id"), "(null)");
}

TEST_F(PgQueueTest, FailIsFencedOnOwnershipToo) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto stranger = queue_->Fail({.id = Id(1), .owner = "worker-2"}, "not mine to fail");
  ASSERT_TRUE(stranger.ok()) << stranger.status();
  EXPECT_FALSE(*stranger);
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING");

  const auto owner = queue_->Fail({.id = Id(1), .owner = "worker-1"}, "chess.com said no");
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

TEST_F(PgQueueTest, WillNotHandBackARowWeStillHoldToOurselves) {
  // A live claim of ours is work in progress, not work available. Offering
  // it again would have one process running the same range twice — and
  // after a shutdown that left the row owned, immediately and forever.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto again = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(again.ok()) << again.status();
  EXPECT_FALSE(again->has_value());
}

TEST_F(PgQueueTest, AQueueThatOwnsItsConnectionClaimsOverIt) {
  // What every indexing thread gets, so that a heartbeat blocked behind
  // one thread's flush does not stall every other thread's writes.
  Insert(Id(1), "hikaru");
  const std::unique_ptr<IndexQueue> owned = NewOwnedPgQueue(conninfo_, kMaxAttempts);

  const auto claimed = owned->ClaimNext("worker-1/0/1", absl::Minutes(5));

  ASSERT_TRUE(claimed.ok()) << claimed.status();
  ASSERT_TRUE(claimed->has_value());
  EXPECT_EQ((*claimed)->player, "hikaru");
  EXPECT_EQ(Column(Id(1), "owner_id"), "worker-1/0/1");
}

TEST_F(PgQueueTest, AnotherRunReclaimingAnExpiredRowSpendsAnAttempt) {
  // The pair below is why a run claims under its own id and not the
  // process's. Sharing one would mean a wedged run's row is reclaimed for
  // free by the next run, so a request that wedges every run it touches
  // never reaches the budget and nothing retires it — while it sits at
  // the head of created_at ASC.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1/1", absl::Seconds(-1)).ok());

  const auto again = queue_->ClaimNext("worker-1/2", absl::Minutes(5));
  ASSERT_TRUE(again.ok()) << again.status();
  ASSERT_TRUE(again->has_value());
  EXPECT_EQ((*again)->attempts, 2);
}

TEST_F(PgQueueTest, ReclaimingOurOwnExpiredRowDoesNotSpendAnAttempt) {
  // We are picking up where we left off, not making a fresh attempt.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Seconds(-1)).ok());

  const auto again = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(again.ok()) << again.status();
  ASSERT_TRUE(again->has_value());
  EXPECT_EQ((*again)->attempts, 1);
}

TEST_F(PgQueueTest, TerminalWritesAreRefusedOnceTheLeaseHasExpired) {
  // At expiry a takeover is already licensed, even before a rival has
  // written its own owner_id. The old owner must not win that race.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Seconds(-1)).ok());

  const auto completed = queue_->Complete({.id = Id(1), .owner = "worker-1"}, 42);
  ASSERT_TRUE(completed.ok()) << completed.status();
  EXPECT_FALSE(*completed);

  const auto failed = queue_->Fail({.id = Id(1), .owner = "worker-1"}, "too late");
  ASSERT_TRUE(failed.ok()) << failed.status();
  EXPECT_FALSE(*failed);
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING");
}

TEST_F(PgQueueTest, TerminalWritesLandWhileTheLeaseIsLive) {
  // The control for the test above: same call, unexpired lease.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto completed = queue_->Complete({.id = Id(1), .owner = "worker-1"}, 42);
  ASSERT_TRUE(completed.ok()) << completed.status();
  EXPECT_TRUE(*completed);
}

TEST_F(PgQueueTest, HandBackFreesTheRowAndRefundsTheAttempt) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());
  ASSERT_EQ(Column(Id(1), "attempts"), "1");

  const auto handed = queue_->HandBack({.id = Id(1), .owner = "worker-1"});
  ASSERT_TRUE(handed.ok()) << handed.status();
  EXPECT_TRUE(*handed);
  EXPECT_EQ(Column(Id(1), "owner_id"), "(null)");
  EXPECT_EQ(Column(Id(1), "attempts"), "0") << "a shutdown is not an attempt at the request";

  // And it is claimable again straight away, by anyone.
  const auto taken = queue_->ClaimNext("worker-2", absl::Minutes(5));
  ASSERT_TRUE(taken.ok()) << taken.status();
  EXPECT_TRUE(taken->has_value());
}

TEST_F(PgQueueTest, ReleaseFreesTheRowAndKeepsTheAttemptSpent) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto released = queue_->Release({.id = Id(1), .owner = "worker-1"});
  ASSERT_TRUE(released.ok()) << released.status();
  EXPECT_TRUE(*released);
  EXPECT_EQ(Column(Id(1), "owner_id"), "(null)");
  EXPECT_EQ(Column(Id(1), "attempts"), "1") << "the range really was tried";
}

TEST_F(PgQueueTest, HandBackAndReleaseAreFencedToo) {
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(queue_->ClaimNext("worker-1", absl::Minutes(5)).ok());

  const auto handed = queue_->HandBack({.id = Id(1), .owner = "worker-2"});
  ASSERT_TRUE(handed.ok()) << handed.status();
  EXPECT_FALSE(*handed);

  const auto released = queue_->Release({.id = Id(1), .owner = "worker-2"});
  ASSERT_TRUE(released.ok()) << released.status();
  EXPECT_FALSE(*released);
  EXPECT_EQ(Column(Id(1), "owner_id"), "worker-1");
}

TEST_F(PgQueueTest, StopsClaimingAfterTooManyAttempts) {
  // A range that fails every time is a request nobody should keep retrying.
  Insert(Id(1), "hikaru");
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET attempts = $1 WHERE id = $2",
                         {std::to_string(kMaxAttempts), Id(1)})
                  .ok());

  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok()) << claimed.status();
  EXPECT_FALSE(claimed->has_value());
}

TEST_F(PgQueueTest, ProgressMovesTheCountWithoutFinishingTheRun) {
  Insert(Id(1), "alice");
  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok() && claimed->has_value());

  const auto recorded = queue_->Progress({.id = Id(1), .owner = "worker-1"}, 42);
  ASSERT_TRUE(recorded.ok()) << recorded.status();
  EXPECT_TRUE(*recorded);
  EXPECT_EQ(Column(Id(1), "games_indexed"), "42");
  EXPECT_EQ(Column(Id(1), "status"), "PROCESSING") << "progress is not an ending";
  EXPECT_EQ(Column(Id(1), "owner_id"), "worker-1") << "nor a handing back";
}

TEST_F(PgQueueTest, ProgressIsFencedOnOwnershipToo) {
  // A run that lost the range must not walk the counter of a row somebody
  // else is now working.
  Insert(Id(1), "alice");
  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok() && claimed->has_value());

  const auto refused = queue_->Progress({.id = Id(1), .owner = "worker-2"}, 42);
  ASSERT_TRUE(refused.ok()) << refused.status();
  EXPECT_FALSE(*refused);
  EXPECT_EQ(Column(Id(1), "games_indexed"), "0");
}

TEST_F(PgQueueTest, ProgressIsRefusedOnceTheLeaseHasExpired) {
  Insert(Id(1), "alice");
  const auto claimed = queue_->ClaimNext("worker-1", absl::Minutes(5));
  ASSERT_TRUE(claimed.ok() && claimed->has_value());
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET lease_expires_at = NOW() -"
                         " INTERVAL '1 minute' WHERE id = $1 RETURNING id",
                         {Id(1)})
                  .ok());

  const auto refused = queue_->Progress({.id = Id(1), .owner = "worker-1"}, 42);
  ASSERT_TRUE(refused.ok()) << refused.status();
  EXPECT_FALSE(*refused);
  EXPECT_EQ(Column(Id(1), "games_indexed"), "0");
}

}  // namespace
}  // namespace one_d4_worker
