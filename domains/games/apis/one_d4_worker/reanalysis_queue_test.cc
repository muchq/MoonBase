#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

// The same rules PgQueueTest pins, on the second queue: a claim is
// exclusive, every terminal write is fenced on a live lease, and an expired
// lease is claimable by anyone. Rules about concurrent SQL, so a real
// Postgres — these skip without PG_TEST_DB_URL.
//
// Plus the one this queue has and the other does not: a pass that loses its
// lease resumes from its cursor.

/// This suite's own schema, for the reason pg_game_sink_test gives: it and
/// pg_reanalysis_test both drop and recreate reanalysis_requests, and bazel
/// runs them at the same time against one database.
constexpr char kSchema[] = "one_d4_reanalysis_queue_test";

/// Carried in the conninfo rather than SET, because pg::Client reconnects
/// lazily and a reconnect drops a session-level setting silently.
///
/// Not UTC, deliberately: every fence here compares lease_expires_at to
/// NOW(), and under a UTC session a TIMESTAMPTZ column would agree with a
/// TIMESTAMP one — so the fixture's type could drift from production and
/// nothing here would notice.
std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema,
                      "%20-c%20timezone%3DAmerica/New_York");
}

class ReanalysisQueueTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    {
      pg::Client bootstrap(url);
      ASSERT_TRUE(bootstrap.Exec(absl::StrCat("CREATE SCHEMA IF NOT EXISTS ", kSchema)).ok());
    }
    client_ = std::make_unique<pg::Client>(Conninfo(url));

    // Column for column what PostgresSqlDialect creates —
    // schema_contract_test is what keeps this copy honest.
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS reanalysis_requests").ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE reanalysis_requests (
            id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
            created_at       TIMESTAMP NOT NULL DEFAULT now(),
            updated_at       TIMESTAMP NOT NULL DEFAULT now(),
            owner_id         VARCHAR(128),
            lease_expires_at TIMESTAMP,
            attempts         INT NOT NULL DEFAULT 0,
            error_message    TEXT,
            cursor_game_url  VARCHAR(1024),
            games_processed  INT NOT NULL DEFAULT 0,
            games_failed     INT NOT NULL DEFAULT 0
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec("CREATE UNIQUE INDEX idx_reanalysis_requests_single_live ON "
                           "reanalysis_requests ((true)) WHERE status IN ('PENDING', "
                           "'PROCESSING')")
                    .ok());
    queue_ = std::make_unique<PgReanalysisQueue>(*client_);
  }

  /// Enqueues a pending pass and returns its id.
  std::string Enqueue() {
    auto inserted =
        client_->Exec("INSERT INTO reanalysis_requests (status) VALUES ('PENDING') RETURNING id");
    EXPECT_TRUE(inserted.ok());
    EXPECT_EQ(inserted->rows(), 1);
    return inserted->Get(0, 0).value_or("");
  }

  std::string Column(const std::string& id, const std::string& column) {
    auto row = client_->Exec("SELECT " + column + " FROM reanalysis_requests WHERE id = $1", {id});
    EXPECT_TRUE(row.ok());
    EXPECT_EQ(row->rows(), 1);
    return row->Get(0, 0).value_or("");
  }

  void ExpireLease(const std::string& id) {
    ASSERT_TRUE(client_
                    ->Exec("UPDATE reanalysis_requests SET lease_expires_at = NOW() - "
                           "INTERVAL '1 minute' WHERE id = $1",
                           {id})
                    .ok());
  }

  std::unique_ptr<pg::Client> client_;
  std::unique_ptr<PgReanalysisQueue> queue_;
};

constexpr absl::Duration kLease = absl::Minutes(5);

TEST_F(ReanalysisQueueTest, ClaimsNothingFromAnEmptyQueue) {
  auto claimed = queue_->ClaimNext("worker-a", kLease);
  ASSERT_TRUE(claimed.ok());
  EXPECT_FALSE(claimed->has_value());
}

TEST_F(ReanalysisQueueTest, ClaimsAPendingPassWithNoCursorYet) {
  const std::string id = Enqueue();

  auto claimed = queue_->ClaimNext("worker-a", kLease);
  ASSERT_TRUE(claimed.ok());
  ASSERT_TRUE(claimed->has_value());
  EXPECT_EQ((*claimed)->id, id);
  EXPECT_EQ((*claimed)->cursor_game_url, "")
      << "a fresh pass starts at the beginning of the corpus";
  EXPECT_EQ((*claimed)->games_processed, 0);
  EXPECT_EQ((*claimed)->games_failed, 0);
  EXPECT_EQ((*claimed)->attempts, 1);
  EXPECT_EQ(Column(id, "status"), "PROCESSING");
}

TEST_F(ReanalysisQueueTest, TwoWorkersCannotClaimTheSamePass) {
  Enqueue();

  auto first = queue_->ClaimNext("worker-a", kLease);
  auto second = queue_->ClaimNext("worker-b", kLease);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_TRUE(first->has_value());
  EXPECT_FALSE(second->has_value())
      << "a reanalysis pass is a single-owner walk of the whole corpus; two of them "
         "double every write";
}

TEST_F(ReanalysisQueueTest, ClaimsAgainOnceTheLeaseHasExpired) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());
  ExpireLease(id);

  auto taken = queue_->ClaimNext("worker-b", kLease);
  ASSERT_TRUE(taken.ok());
  ASSERT_TRUE(taken->has_value());
  EXPECT_EQ((*taken)->attempts, 2) << "a different owner spends an attempt";
}

// The property the cursor exists for. A pass that dies two thirds of the way
// through must not start the corpus again — that is the OFFSET paging's
// failure, and on a corpus this size it is the difference between resuming
// and never finishing.
TEST_F(ReanalysisQueueTest, ATakeoverResumesFromTheCursor) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());

  auto recorded = queue_->Progress(id, "worker-a", "https://chess.com/game/500", 500, 3);
  ASSERT_TRUE(recorded.ok());
  EXPECT_TRUE(*recorded);

  ExpireLease(id);
  auto taken = queue_->ClaimNext("worker-b", kLease);
  ASSERT_TRUE(taken.ok());
  ASSERT_TRUE(taken->has_value());
  EXPECT_EQ((*taken)->cursor_game_url, "https://chess.com/game/500");
  EXPECT_EQ((*taken)->games_processed, 500);
  EXPECT_EQ((*taken)->games_failed, 3);
}

TEST_F(ReanalysisQueueTest, ProgressIsFencedOnALiveLease) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());

  auto stranger = queue_->Progress(id, "worker-b", "https://chess.com/game/9", 9, 0);
  ASSERT_TRUE(stranger.ok());
  EXPECT_FALSE(*stranger) << "a worker that does not hold the row must not move its cursor";

  ExpireLease(id);
  auto expired = queue_->Progress(id, "worker-a", "https://chess.com/game/9", 9, 0);
  ASSERT_TRUE(expired.ok());
  EXPECT_FALSE(*expired)
      << "at expiry a takeover is already licensed, so the old owner must not win that race";
}

TEST_F(ReanalysisQueueTest, CompleteIsFencedOnOwnership) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());

  auto stranger = queue_->Complete(id, "worker-b", 10, 0);
  ASSERT_TRUE(stranger.ok());
  EXPECT_FALSE(*stranger);
  EXPECT_EQ(Column(id, "status"), "PROCESSING");

  auto owner = queue_->Complete(id, "worker-a", 10, 1);
  ASSERT_TRUE(owner.ok());
  EXPECT_TRUE(*owner);
  EXPECT_EQ(Column(id, "status"), "COMPLETED");
  EXPECT_EQ(Column(id, "games_processed"), "10");
  EXPECT_EQ(Column(id, "games_failed"), "1");
}

TEST_F(ReanalysisQueueTest, FailIsFencedOnOwnershipToo) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());

  auto stranger = queue_->Fail(id, "worker-b", "not mine to fail");
  ASSERT_TRUE(stranger.ok());
  EXPECT_FALSE(*stranger);

  auto owner = queue_->Fail(id, "worker-a", "pg went away");
  ASSERT_TRUE(owner.ok());
  EXPECT_TRUE(*owner);
  EXPECT_EQ(Column(id, "status"), "FAILED");
  EXPECT_EQ(Column(id, "error_message"), "pg went away");
}

TEST_F(ReanalysisQueueTest, WillNotClaimAPassThatIsAlreadyFinished) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());
  ASSERT_TRUE(queue_->Complete(id, "worker-a", 1, 0).ok());

  auto again = queue_->ClaimNext("worker-b", kLease);
  ASSERT_TRUE(again.ok());
  EXPECT_FALSE(again->has_value());
}

TEST_F(ReanalysisQueueTest, HandBackRefundsTheAttemptAndReleaseSpendsIt) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());
  EXPECT_EQ(Column(id, "attempts"), "1");

  // A shutdown is not the pass's fault.
  auto handed = queue_->HandBack(id, "worker-a");
  ASSERT_TRUE(handed.ok());
  EXPECT_TRUE(*handed);
  EXPECT_EQ(Column(id, "attempts"), "0");

  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());
  EXPECT_EQ(Column(id, "attempts"), "1");

  // Hitting its own ceiling is. Refunding that would retry forever.
  auto released = queue_->Release(id, "worker-a");
  ASSERT_TRUE(released.ok());
  EXPECT_TRUE(*released);
  EXPECT_EQ(Column(id, "attempts"), "1");
}

// The ceiling refund and every shutdown depend on this: both give the row
// back expecting whoever takes it next to resume, and a HandBack that
// cleared the cursor would restart a large corpus after every refund with
// the whole suite green.
TEST_F(ReanalysisQueueTest, HandBackAndReleaseLeaveTheCursorForTheNextOwner) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());
  ASSERT_TRUE(queue_->Progress(id, "worker-a", "https://chess.com/game/0500", 500, 3).ok());
  ASSERT_TRUE(queue_->HandBack(id, "worker-a").ok());

  auto after_handback = queue_->ClaimNext("worker-b", kLease);
  ASSERT_TRUE(after_handback.ok());
  ASSERT_TRUE(after_handback->has_value());
  EXPECT_EQ((*after_handback)->cursor_game_url, "https://chess.com/game/0500");
  EXPECT_EQ((*after_handback)->games_processed, 500);
  EXPECT_EQ((*after_handback)->games_failed, 3);

  ASSERT_TRUE(queue_->Progress(id, "worker-b", "https://chess.com/game/0700", 700, 4).ok());
  ASSERT_TRUE(queue_->Release(id, "worker-b").ok());

  auto after_release = queue_->ClaimNext("worker-c", kLease);
  ASSERT_TRUE(after_release.ok());
  ASSERT_TRUE(after_release->has_value());
  EXPECT_EQ((*after_release)->cursor_game_url, "https://chess.com/game/0700");
  EXPECT_EQ((*after_release)->games_processed, 700);
}

TEST_F(ReanalysisQueueTest, StopsClaimingAfterTheAttemptBudget) {
  const std::string id = Enqueue();
  for (int i = 0; i < PgReanalysisQueue::kMaxAttempts; ++i) {
    auto claimed = queue_->ClaimNext("worker-" + std::to_string(i), kLease);
    ASSERT_TRUE(claimed.ok());
    ASSERT_TRUE(claimed->has_value()) << "attempt " << i;
    ExpireLease(id);
  }

  auto exhausted = queue_->ClaimNext("worker-last", kLease);
  ASSERT_TRUE(exhausted.ok());
  EXPECT_FALSE(exhausted->has_value())
      << "a pass that has killed its worker three times will kill the fourth";

  // Retired, not merely unclaimable. Left PROCESSING it would hold the
  // single-live slot forever: never claimable again, never history, and
  // every later enqueue refused by idx_reanalysis_requests_single_live.
  EXPECT_EQ(Column(id, "status"), "FAILED");
  auto next = client_->Exec("INSERT INTO reanalysis_requests (status) VALUES ('PENDING')");
  EXPECT_TRUE(next.ok()) << "the exhausted pass still holds the live slot: " << next.status();
}

TEST_F(ReanalysisQueueTest, HeartbeatHoldsTheLeaseAndSaysWhenItIsLost) {
  const std::string id = Enqueue();
  ASSERT_TRUE(queue_->ClaimNext("worker-a", kLease).ok());

  auto held = queue_->Heartbeat(id, "worker-a", kLease);
  ASSERT_TRUE(held.ok());
  EXPECT_TRUE(*held);

  auto stranger = queue_->Heartbeat(id, "worker-b", kLease);
  ASSERT_TRUE(stranger.ok());
  EXPECT_FALSE(*stranger);
}

// The forwarder worker_main actually claims through. Seven hand-written
// methods, every one of which takes id and owner in that order — swap them
// in any single one and the fence stops matching, so production reanalysis
// writes nothing, forever, in silence.
TEST_F(ReanalysisQueueTest, AQueueThatOwnsItsConnectionClaimsAndFencesLikeAnyOther) {
  const std::string id = Enqueue();
  const std::unique_ptr<ReanalysisQueue> owned =
      NewOwnedReanalysisQueue(Conninfo(std::getenv("PG_TEST_DB_URL")));

  auto claimed = owned->ClaimNext("worker-a", kLease);
  ASSERT_TRUE(claimed.ok());
  ASSERT_TRUE(claimed->has_value());
  EXPECT_EQ((*claimed)->id, id);

  EXPECT_TRUE(*owned->Heartbeat(id, "worker-a", kLease));
  EXPECT_FALSE(*owned->Heartbeat(id, "worker-b", kLease));

  EXPECT_TRUE(*owned->Progress(id, "worker-a", "https://chess.com/game/0009", 9, 1));
  EXPECT_FALSE(*owned->Progress(id, "worker-b", "https://chess.com/game/0009", 9, 1));

  EXPECT_FALSE(*owned->Complete(id, "worker-b", 9, 1));
  EXPECT_TRUE(*owned->Complete(id, "worker-a", 9, 1));
  EXPECT_EQ(Column(id, "status"), "COMPLETED");
  EXPECT_EQ(Column(id, "cursor_game_url"), "https://chess.com/game/0009");
}

TEST_F(ReanalysisQueueTest, AnOwnedQueueHandsBackAndReleasesUnderTheRightOwnerToo) {
  const std::string id = Enqueue();
  const std::unique_ptr<ReanalysisQueue> owned =
      NewOwnedReanalysisQueue(Conninfo(std::getenv("PG_TEST_DB_URL")));
  ASSERT_TRUE(owned->ClaimNext("worker-a", kLease).ok());

  EXPECT_FALSE(*owned->HandBack(id, "worker-b"));
  EXPECT_TRUE(*owned->HandBack(id, "worker-a"));
  EXPECT_EQ(Column(id, "attempts"), "0");

  ASSERT_TRUE(owned->ClaimNext("worker-a", kLease).ok());
  EXPECT_FALSE(*owned->Release(id, "worker-b"));
  EXPECT_TRUE(*owned->Release(id, "worker-a"));
  EXPECT_EQ(Column(id, "attempts"), "1");
}

TEST_F(ReanalysisQueueTest, AnOwnedQueueFailsUnderTheRightOwner) {
  const std::string id = Enqueue();
  const std::unique_ptr<ReanalysisQueue> owned =
      NewOwnedReanalysisQueue(Conninfo(std::getenv("PG_TEST_DB_URL")));
  ASSERT_TRUE(owned->ClaimNext("worker-a", kLease).ok());

  EXPECT_FALSE(*owned->Fail(id, "worker-b", "not mine"));
  EXPECT_TRUE(*owned->Fail(id, "worker-a", "pg went away"));
  EXPECT_EQ(Column(id, "error_message"), "pg went away");
}

}  // namespace
}  // namespace one_d4_worker
