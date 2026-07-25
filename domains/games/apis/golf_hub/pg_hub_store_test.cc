#include "domains/games/apis/golf_hub/pg_hub_store.h"

#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/apis/golf_hub/migrations.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"
#include "domains/platform/libs/pg/pg.h"
#include "gtest/gtest.h"

namespace {

using golf_hub::PgHubStore;

// A real engine state for the started-game rows — the store owns the
// serde end to end now, so round-trip fidelity is asserted by canonical
// re-serialization.
golf::GameState DealtState() {
  std::deque<cards::Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  auto dealt = golf::dealGolfGame("G2", {"alice", "bob"}, std::move(deck));
  EXPECT_TRUE(dealt.ok());
  return *std::move(dealt);
}

// Step-2 slice of the persistence integration suite (#1194): the
// write-through ops against the real tables. GOLF_HUB_TEST_DB_URL gates
// it like the rest of the suite.
class PgHubStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url == nullptr || *url == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    db_ = std::make_shared<pg::Client>(url);
    ASSERT_TRUE(golf_hub::RunMigrations(*db_).ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE rooms CASCADE").ok());
    store_ = std::make_unique<PgHubStore>(db_);
  }

  std::shared_ptr<pg::Client> db_;
  std::unique_ptr<PgHubStore> store_;
};

TEST_F(PgHubStoreTest, OpsRoundTripThroughSnapshot) {
  PgHubStore::MemberRow alice{"R1", "alice", true, 2, 1, 9};
  PgHubStore::MemberRow bob{"R1", "bob", false, 2, 0, 14};
  const golf::GameState state = DealtState();
  PgHubStore::GameRow waiting{"R1", "G1", {"alice"}, std::nullopt, 1};
  PgHubStore::GameRow started{"R1", "G2", {"alice", "bob"}, state, 1};
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertMember{alice},
                   PgHubStore::UpsertMember{bob}, PgHubStore::SaveGame{waiting},
                   PgHubStore::SaveGame{started}});
  store_->Flush();

  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  ASSERT_EQ(snapshot->rooms.size(), 1u);
  EXPECT_EQ(snapshot->rooms[0], "R1");
  ASSERT_EQ(snapshot->members.size(), 2u);
  ASSERT_EQ(snapshot->games.size(), 2u);
  for (const auto& game : snapshot->games) {
    if (game.game_id == "G1") {
      EXPECT_FALSE(game.state.has_value());
      EXPECT_EQ(game.roster, (std::vector<std::string>{"alice"}));
      EXPECT_EQ(game.version, 1);
    } else {
      EXPECT_EQ(game.game_id, "G2");
      ASSERT_TRUE(game.state.has_value());
      // Canonical re-serialization is state equality.
      EXPECT_EQ(golf::serializeGameState(*game.state), golf::serializeGameState(state));
      EXPECT_EQ(game.version, 1);
    }
  }

  // Upserts converge on the latest value; deletes remove exactly their row.
  alice.total_score = 12;
  alice.connected = false;
  store_->Enqueue({PgHubStore::UpsertMember{alice}, PgHubStore::DeleteMember{"R1", "bob"},
                   PgHubStore::DeleteGame{"R1", "G1"}});
  store_->Flush();
  snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->members.size(), 1u);
  EXPECT_EQ(snapshot->members[0].total_score, 12);
  EXPECT_FALSE(snapshot->members[0].connected);
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].game_id, "G2");
}

TEST_F(PgHubStoreTest, ConditionalSaveAdvancesAndRepairsOnDivergence) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"},
                   PgHubStore::SaveGame{{"R1", "G1", {"alice"}, std::nullopt, 1}},
                   PgHubStore::SaveGame{{"R1", "G1", {"alice", "bob"}, std::nullopt, 2}}});
  store_->Flush();
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].version, 2);

  // A save that skips a version means the DB diverged from the only
  // writer: it must still land (last-writer-wins repair), loudly.
  store_->Enqueue({PgHubStore::SaveGame{{"R1", "G1", {"alice", "carol"}, std::nullopt, 5}}});
  store_->Flush();
  snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].version, 5);
  EXPECT_EQ(snapshot->games[0].roster, (std::vector<std::string>{"alice", "carol"}));
}

TEST_F(PgHubStoreTest, DeleteRoomCascades) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"},
                   PgHubStore::UpsertMember{{"R1", "alice", true, 0, 0, 0}},
                   PgHubStore::SaveGame{{"R1", "G1", {"alice"}, std::nullopt, 1}},
                   PgHubStore::DeleteRoom{"R1"}});
  store_->Flush();
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->rooms.empty());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
}

}  // namespace
