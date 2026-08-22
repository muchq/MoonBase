#include "domains/iili/apis/iili/pg_url_store.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/iili/apis/iili/migrations.h"
#include "domains/platform/libs/pg/pg.h"

// Real SQL against real Postgres; skips without PG_TEST_DB_URL.

namespace iili {
namespace {

class PgUrlStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') {
      GTEST_SKIP() << "PG_TEST_DB_URL unset";
    }
    db_ = std::make_shared<pg::Client>(url);
    ASSERT_TRUE(RunMigrations(*db_).ok());
    // Second run is a no-op: idempotency is the whole migration mechanism.
    ASSERT_TRUE(RunMigrations(*db_).ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE urls").ok());
    store_ = std::make_unique<PgUrlStore>(db_, db_);
  }

  std::shared_ptr<pg::Client> db_;
  std::unique_ptr<PgUrlStore> store_;
};

TEST_F(PgUrlStoreTest, InsertThenLookupRoundTrips) {
  const absl::Time expires = absl::Now() + absl::Hours(1);
  const auto slug = store_->Insert("https://example.com/live", expires);
  ASSERT_TRUE(slug.ok()) << slug.status();
  EXPECT_GE(slug->size(), 3u);

  const auto found = store_->Lookup(*slug);
  ASSERT_TRUE(found.ok()) << found.status();
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ((*found)->long_url, "https://example.com/live");
  // Millis-precision round trip through to_timestamp and extract(epoch).
  EXPECT_EQ(absl::ToUnixMillis((*found)->expires_at), absl::ToUnixMillis(expires));
}

TEST_F(PgUrlStoreTest, AnUnknownSlugIsANulloptNotAnError) {
  const auto found = store_->Lookup("zzz");
  ASSERT_TRUE(found.ok());
  EXPECT_FALSE(found->has_value());
}

// Expiry is enforced in the SQL: expired and absent are indistinguishable.
TEST_F(PgUrlStoreTest, AnExpiredRowDoesNotResolve) {
  const auto slug = store_->Insert("https://example.com/dead", absl::Now() - absl::Seconds(1));
  ASSERT_TRUE(slug.ok());

  const auto found = store_->Lookup(*slug);
  ASSERT_TRUE(found.ok());
  EXPECT_FALSE(found->has_value());

  // Control: the row exists; the predicate is what hides it.
  const auto raw = db_->Exec("SELECT count(*) FROM urls WHERE short_url = $1", {*slug});
  ASSERT_TRUE(raw.ok());
  EXPECT_EQ(*raw->Get(0, 0), "1");
}

// Re-running migrations must not disturb data — the stronger half of
// "safe to re-run".
TEST_F(PgUrlStoreTest, RerunningMigrationsPreservesRows) {
  const auto slug = store_->Insert("https://example.com/keep", absl::Now() + absl::Hours(1));
  ASSERT_TRUE(slug.ok());
  ASSERT_TRUE(RunMigrations(*db_).ok());
  const auto found = store_->Lookup(*slug);
  ASSERT_TRUE(found.ok());
  EXPECT_TRUE(found->has_value());
}

TEST_F(PgUrlStoreTest, ConsecutiveInsertsMintDistinctSlugs) {
  const absl::Time expires = absl::Now() + absl::Hours(1);
  const auto first = store_->Insert("https://example.com/a", expires);
  const auto second = store_->Insert("https://example.com/b", expires);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_NE(*first, *second);
}

// The reconnect-retry contract: replaying an identical insert is success
// and the same slug. The sequence rewind forces the same id twice.
TEST_F(PgUrlStoreTest, AReplayedInsertIsSuccessNotAConflictError) {
  const absl::Time expires = absl::Now() + absl::Hours(1);
  const auto first = store_->Insert("https://example.com/x", expires);
  ASSERT_TRUE(first.ok()) << first.status();

  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', (SELECT last_value FROM url_ids) - 1)").ok());
  const auto replayed = store_->Insert("https://example.com/x", expires);
  ASSERT_TRUE(replayed.ok()) << "the replay must not surface as an error";
  EXPECT_EQ(*replayed, *first);
}

// Same URL is not enough: a different expiry means an import's row, and
// success would cache an expiry the row doesn't hold.
TEST_F(PgUrlStoreTest, ASameUrlConflictWithADifferentExpiryRefuses) {
  const absl::Time expires = absl::Now() + absl::Hours(1);
  ASSERT_TRUE(store_->Insert("https://example.com/x", expires).ok());
  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', (SELECT last_value FROM url_ids) - 1)").ok());

  const auto refused = store_->Insert("https://example.com/x", expires + absl::Hours(1));
  EXPECT_FALSE(refused.ok());
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInternal);
}

// Matching URL and expiry still refuse when the row's slug isn't ours —
// an import that encoded ids differently.
TEST_F(PgUrlStoreTest, AForeignSlugAtOurIdRefusesEvenWithMatchingFields) {
  const int64_t millis = absl::ToUnixMillis(absl::Now() + absl::Hours(1));
  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', 100)").ok());
  ASSERT_TRUE(db_->Exec("INSERT INTO urls (id, short_url, long_url, expires_at)"
                        " VALUES (101, 'xxx', 'https://example.com/mine',"
                        " to_timestamp($1::bigint / 1000.0))",
                        {absl::StrCat(millis)})
                  .ok());

  const auto refused = store_->Insert("https://example.com/mine", absl::FromUnixMillis(millis));
  EXPECT_FALSE(refused.ok());
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInternal);
}

// Floor keeps v2's next ids out of the low shared-encoder space v1 used,
// and re-floors a rewound sequence on the next boot. nextval, not
// last_value: an is_called flip would hand out 1000000 itself, inside
// that low space.
TEST_F(PgUrlStoreTest, MigrationsFloorTheSequenceAboveV1sIdSpace) {
  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', 3)").ok());
  ASSERT_TRUE(RunMigrations(*db_).ok());

  const auto next = db_->Exec("SELECT nextval('url_ids')");
  ASSERT_TRUE(next.ok());
  int64_t id = 0;
  ASSERT_TRUE(absl::SimpleAtoi(*next->Get(0, 0), &id));
  EXPECT_GT(id, 1000000);
}

// The floor is a floor, not a reset: a sequence already past it stays put.
TEST_F(PgUrlStoreTest, RemigratingAboveTheFloorStaysMonotone) {
  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', 2000000)").ok());
  ASSERT_TRUE(RunMigrations(*db_).ok());

  const auto next = db_->Exec("SELECT nextval('url_ids')");
  ASSERT_TRUE(next.ok());
  int64_t id = 0;
  ASSERT_TRUE(absl::SimpleAtoi(*next->Get(0, 0), &id));
  EXPECT_GT(id, 2000000);
}

// Rows minted below the floor (early v2 deploys) are cleared so Lookup
// 404s those slugs even on a database that saw a pre-floor mint.
TEST_F(PgUrlStoreTest, MigrationsClearRowsMintedBelowTheFloor) {
  ASSERT_TRUE(db_->Exec("INSERT INTO urls (id, short_url, long_url, expires_at)"
                        " VALUES (42, 'KgA', 'https://example.com/prefloor',"
                        " now() + interval '1 hour')")
                  .ok());
  ASSERT_TRUE(RunMigrations(*db_).ok());

  const auto found = store_->Lookup("KgA");
  ASSERT_TRUE(found.ok());
  EXPECT_FALSE(found->has_value());
}

// A sequence behind the table (an import without setval) must refuse, not
// alias someone else's slug.
TEST_F(PgUrlStoreTest, ASequenceBehindTheTableRefusesToAliasASlug) {
  ASSERT_TRUE(db_->Exec("SELECT setval('url_ids', 100)").ok());
  ASSERT_TRUE(db_->Exec("INSERT INTO urls (id, short_url, long_url, expires_at)"
                        " VALUES (101, 'ZQA', 'https://example.com/theirs',"
                        " now() + interval '1 hour')")
                  .ok());

  const auto refused = store_->Insert("https://example.com/mine", absl::Now() + absl::Hours(1));
  EXPECT_FALSE(refused.ok());
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInternal);
}

// The CHECK refuses what routes around the generated server.
TEST_F(PgUrlStoreTest, TheCheckConstraintRefusesAnOversizedUrl) {
  const std::string oversized(4001, 'a');
  const auto refused = db_->Exec(
      "INSERT INTO urls (id, short_url, long_url, expires_at)"
      " VALUES (43, 'KwA', $1, now() + interval '1 hour')",
      {oversized});
  EXPECT_FALSE(refused.ok());
}

}  // namespace
}  // namespace iili
