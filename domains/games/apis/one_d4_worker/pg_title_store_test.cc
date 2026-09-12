#include "domains/games/apis/one_d4_worker/pg_title_store.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/migration_files.h"
#include "domains/games/apis/one_d4_worker/pg_test_db.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

// The upsert's ordering rule is a property of two statements meeting on one
// row, so it is tested against a real Postgres — see TestDbUrl for what a
// missing one does.

/// A schema of this suite's own; every suite here shares the one database.
constexpr char kSchema[] = "one_d4_pg_title_store_test";

std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema);
}

class PgTitleStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    const absl::StatusOr<std::string> db_url = TestDbUrl();
    if (absl::IsUnavailable(db_url.status())) GTEST_SKIP() << db_url.status().message();
    ASSERT_TRUE(db_url.ok()) << db_url.status();
    client_ = std::make_unique<pg::Client>(Conninfo(*db_url));
    ASSERT_TRUE(ResetToMigratedSchema(*client_, kSchema).ok());
    store_ = std::make_unique<PgTitleStore>(*client_);
  }

  std::string Stored(const std::string& platform, const std::string& username,
                     const std::string& column) {
    const auto result =
        client_->Exec(absl::StrCat("SELECT ", column,
                                   " FROM player_titles WHERE platform = $1 AND username = $2"),
                      {platform, username});
    EXPECT_TRUE(result.ok()) << result.status();
    if (!result.ok() || result->rows() == 0) return "(none)";
    return result->Get(0, 0).value_or("(null)");
  }

  std::unique_ptr<pg::Client> client_;
  std::unique_ptr<PgTitleStore> store_;
};

TEST_F(PgTitleStoreTest, ReadsBackWhatItWrote) {
  ASSERT_TRUE(
      store_->Save("CHESS_COM", {{"hikaru", "GM"}, {"someone", "WIM"}}, absl::FromUnixSeconds(1000))
          .ok());

  const auto loaded = store_->Load("CHESS_COM");
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_THAT(*loaded, ElementsAre(Pair("hikaru", "GM"), Pair("someone", "WIM")));
}

TEST_F(PgTitleStoreTest, AnEmptyTableLoadsEmptyRatherThanFailing) {
  const auto loaded = store_->Load("CHESS_COM");
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_TRUE(loaded->empty());
}

TEST_F(PgTitleStoreTest, SavingNothingIsNotAnError) {
  EXPECT_TRUE(store_->Save("CHESS_COM", {}, absl::FromUnixSeconds(1000)).ok());
}

// A username is a different player on another platform, so one platform's
// observation must never answer another's lookup.
TEST_F(PgTitleStoreTest, KeepsThePlatformsApart) {
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "GM"}}, absl::FromUnixSeconds(1000)).ok());
  ASSERT_TRUE(store_->Save("LICHESS", {{"hikaru", "IM"}}, absl::FromUnixSeconds(1000)).ok());

  EXPECT_THAT(*store_->Load("CHESS_COM"), ElementsAre(Pair("hikaru", "GM")));
  EXPECT_THAT(*store_->Load("LICHESS"), ElementsAre(Pair("hikaru", "IM")));
}

TEST_F(PgTitleStoreTest, ANewerObservationWins) {
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "IM"}}, absl::FromUnixSeconds(1000)).ok());
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "GM"}}, absl::FromUnixSeconds(2000)).ok());

  EXPECT_THAT(*store_->Load("CHESS_COM"), ElementsAre(Pair("hikaru", "GM")));
}

// The reason the upsert carries a WHERE at all. Indexing is not
// chronological: a backfill of 2019 runs after 2026 is indexed, and without
// the guard it would demote a player the roster titled today.
TEST_F(PgTitleStoreTest, AnOlderObservationDoesNotDemoteANewerTitle) {
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "GM"}}, absl::FromUnixSeconds(2000)).ok());
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "IM"}}, absl::FromUnixSeconds(1000)).ok());

  EXPECT_THAT(*store_->Load("CHESS_COM"), ElementsAre(Pair("hikaru", "GM")));
  EXPECT_EQ(Stored("CHESS_COM", "hikaru", "extract(epoch from observed_at)::bigint"), "2000");
}

TEST_F(PgTitleStoreTest, AnObservationAtTheSameInstantLeavesTheRowAlone) {
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "GM"}}, absl::FromUnixSeconds(1000)).ok());
  ASSERT_TRUE(store_->Save("CHESS_COM", {{"hikaru", "IM"}}, absl::FromUnixSeconds(1000)).ok());

  EXPECT_THAT(*store_->Load("CHESS_COM"), ElementsAre(Pair("hikaru", "GM")));
}

// Absence of a title is every untitled player too, so a blank must not be
// stored: a row saying "" would shadow a real title on the next read.
TEST_F(PgTitleStoreTest, NeverStoresAnEmptyTitle) {
  ASSERT_TRUE(
      store_->Save("CHESS_COM", {{"hikaru", "GM"}, {"nobody", ""}}, absl::FromUnixSeconds(1000))
          .ok());

  EXPECT_THAT(*store_->Load("CHESS_COM"), ElementsAre(Pair("hikaru", "GM")));
  EXPECT_EQ(Stored("CHESS_COM", "nobody", "title"), "(none)");
}

// A roster is tens of thousands of players, so Save batches. The batch
// boundary is where a bug would hide.
TEST_F(PgTitleStoreTest, WritesMoreRowsThanOneBatchHolds) {
  TitleMap many;
  const int rows = PgTitleStore::kBatchRows * 2 + 7;
  for (int i = 0; i < rows; ++i) many.emplace(absl::StrCat("player", i), "GM");

  ASSERT_TRUE(store_->Save("CHESS_COM", many, absl::FromUnixSeconds(1000)).ok());

  const auto loaded = store_->Load("CHESS_COM");
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_EQ(static_cast<int>(loaded->size()), rows);
}

}  // namespace
}  // namespace one_d4_worker
