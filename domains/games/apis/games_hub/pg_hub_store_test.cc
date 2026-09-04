#include "domains/games/apis/games_hub/pg_hub_store.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/migrations.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/castle/game_state_serde.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"
#include "domains/platform/libs/pg/listener.h"
#include "domains/platform/libs/pg/pg.h"
#include "gtest/gtest.h"

namespace {

using games_hub::PgHubStore;

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

// Payload sink for the notify assertions. LISTEN lands asynchronously,
// so tests probe with a marker payload until the subscription is live;
// postgres keeps send order after that.
struct Received {
  std::mutex mu;
  std::condition_variable cv;
  std::vector<std::string> payloads;

  void Add(const std::string& payload) {
    const std::lock_guard<std::mutex> lock(mu);
    payloads.push_back(payload);
    cv.notify_all();
  }
  bool Saw(const std::string& want, std::chrono::seconds timeout = std::chrono::seconds(5)) {
    std::unique_lock<std::mutex> lock(mu);
    return cv.wait_for(lock, timeout, [&] {
      return std::find(payloads.begin(), payloads.end(), want) != payloads.end();
    });
  }
};

// Step-2 slice of the persistence integration suite (#1194): the
// write-through ops against the real tables. GAMES_HUB_TEST_DB_URL gates
// it like the rest of the suite.
class PgHubStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    url_ = std::getenv("GAMES_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GAMES_HUB_TEST_DB_URL unset";
    }
    db_ = std::make_shared<pg::Client>(url_);
    ASSERT_TRUE(games_hub::RunMigrations(*db_).ok());
    ASSERT_TRUE(db_->Exec("TRUNCATE rooms CASCADE").ok());
    store_ = std::make_unique<PgHubStore>(db_);
  }

  void ConfirmSubscribed(const std::string& channel, Received& received) {
    bool live = false;
    for (int i = 0; i < 50 && !live; ++i) {
      ASSERT_TRUE(db_->Exec("SELECT pg_notify($1, 'probe')", {channel}).ok());
      live = received.Saw("probe", std::chrono::seconds(1));
    }
    ASSERT_TRUE(live) << "LISTEN on " << channel << " never became live";
  }

  const char* url_ = nullptr;
  std::shared_ptr<pg::Client> db_;
  std::unique_ptr<PgHubStore> store_;
};

// A castle table (#77) stores its kind and decodes with castle's serde;
// an unstarted one carries the kind alone. Rows from before the column
// read as golf, which the migration's default says.
TEST_F(PgHubStoreTest, CastleRowsKeepTheirKindAndDecodeWithCastleSerde) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  PgHubStore::GameRow waiting{"R1", "C1", {"alice"}, std::nullopt, 1, games_hub::GameKind::kCastle};
  ASSERT_TRUE(*store_->CommitGameSave(waiting, ""));
  cards::NoShuffleDealer dealer;
  auto dealt = castle::dealCastleGame("C2", {"alice", "bob"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  PgHubStore::GameRow started{"R1",
                              "C2",
                              {"alice", "bob"},
                              games_hub::HostedState(*dealt),
                              1,
                              games_hub::GameKind::kCastle};
  ASSERT_TRUE(*store_->CommitGameSave(started, ""));

  auto rows = store_->LoadRoom("R1");
  ASSERT_TRUE(rows.ok()) << rows.status();
  ASSERT_EQ(rows->games.size(), 2u);
  for (const auto& game : rows->games) {
    EXPECT_EQ(game.kind, games_hub::GameKind::kCastle) << game.game_id;
    if (game.game_id == "C1") {
      EXPECT_FALSE(game.state.has_value());
      continue;
    }
    ASSERT_TRUE(game.state.has_value());
    ASSERT_TRUE(std::holds_alternative<castle::GameState>(*game.state));
    EXPECT_EQ(castle::serializeGameState(std::get<castle::GameState>(*game.state)),
              castle::serializeGameState(*dealt));
  }
  auto one = store_->LoadGame("R1", "C2");
  ASSERT_TRUE(one.ok() && one->has_value());
  EXPECT_EQ((*one)->kind, games_hub::GameKind::kCastle);

  // A started row's column follows its state, so a row built with the
  // kind left at its default still reads back as the engine it holds.
  PgHubStore::GameRow mislabeled{"R1", "C3", {"alice", "bob"}, games_hub::HostedState(*dealt), 1};
  ASSERT_TRUE(*store_->CommitGameSave(mislabeled, ""));
  auto relabeled = store_->LoadGame("R1", "C3");
  ASSERT_TRUE(relabeled.ok() && relabeled->has_value());
  EXPECT_EQ((*relabeled)->kind, games_hub::GameKind::kCastle);

  // The kind is fixed at creation: a later save neither needs nor
  // changes it.
  PgHubStore::GameRow later{"R1",         "C1", {"alice", "bob"},
                            std::nullopt, 2,    games_hub::GameKind::kGolf};
  ASSERT_TRUE(*store_->CommitGameSave(later, ""));
  auto reread = store_->LoadGame("R1", "C1");
  ASSERT_TRUE(reread.ok() && reread->has_value());
  EXPECT_EQ((*reread)->kind, games_hub::GameKind::kCastle);
  EXPECT_EQ((*reread)->version, 2);
}

// A row written before the kind column reads as golf (the migration's
// default), and a kind no engine plays costs that row alone — the same
// one-bad-row policy as an undecodable state.
TEST_F(PgHubStoreTest, PreColumnRowsReadAsGolfAndAnUnknownKindDropsTheRow) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(db_->Exec("INSERT INTO games (room_id, game_id, roster, state, version)"
                        " VALUES ('R1', 'OLD', '[\"alice\"]'::jsonb, NULL, 1)")
                  .ok());
  auto old = store_->LoadGame("R1", "OLD");
  ASSERT_TRUE(old.ok() && old->has_value());
  EXPECT_EQ((*old)->kind, games_hub::GameKind::kGolf);

  ASSERT_TRUE(db_->Exec("UPDATE games SET game = 'bridge' WHERE game_id = 'OLD'").ok());
  auto gone = store_->LoadGame("R1", "OLD");
  ASSERT_TRUE(gone.ok());
  EXPECT_FALSE(gone->has_value());
  auto rows = store_->LoadRoom("R1");
  ASSERT_TRUE(rows.ok());
  EXPECT_TRUE(rows->games.empty());
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->games.empty());
}

TEST_F(PgHubStoreTest, OpsRoundTripThroughSnapshot) {
  PgHubStore::MemberRow alice{"R1", "alice", true, 2, 1, 9};
  PgHubStore::MemberRow bob{"R1", "bob", false, 2, 0, 14};
  const golf::GameState state = DealtState();
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertMember{alice},
                   PgHubStore::UpsertMember{bob}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, ""));
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G2", {"alice", "bob"}, state, 1}, ""));

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
      EXPECT_EQ(golf::serializeGameState(std::get<golf::GameState>(*game.state)),
                golf::serializeGameState(state));
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

// The step-3 commit path (#1194): the notify must ride exactly the
// commits that land — a conflicted or replayed commit stays silent.
TEST_F(PgHubStoreTest, CommitNotifiesExactlyTheSavesThatLand) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();

  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); },
      /*on_active=*/nullptr);
  listener.Listen(games_hub::RoomChannel("R1"));
  ConfirmSubscribed(games_hub::RoomChannel("R1"), received);

  // Version 1 inserts fresh and notifies.
  auto landed = store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, "v1");
  ASSERT_TRUE(landed.ok()) << landed.status();
  EXPECT_TRUE(*landed);
  EXPECT_TRUE(received.Saw("v1"));

  // The same code again is taken: refused, silent.
  landed = store_->CommitGameSave({"R1", "G1", {"mallory"}, std::nullopt, 1}, "dupe");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);

  // Version 2 follows the stored version 1.
  const golf::GameState state = DealtState();
  landed = store_->CommitGameSave({"R1", "G1", {"alice", "bob"}, state, 2}, "v2");
  ASSERT_TRUE(landed.ok());
  EXPECT_TRUE(*landed);
  EXPECT_TRUE(received.Saw("v2"));

  // Version 4 skips 3: conflict, silent, and the rebase read returns
  // the stored truth to rebuild on.
  landed = store_->CommitGameSave({"R1", "G1", {"alice", "bob"}, state, 4}, "v4");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  auto rebase = store_->LoadGame("R1", "G1");
  ASSERT_TRUE(rebase.ok()) << rebase.status();
  ASSERT_TRUE(rebase->has_value());
  EXPECT_EQ((*rebase)->version, 2);
  ASSERT_TRUE((*rebase)->state.has_value());
  EXPECT_EQ(golf::serializeGameState(std::get<golf::GameState>(*(*rebase)->state)),
            golf::serializeGameState(state));

  // Delivery keeps send order, so a trailing marker proves the refused
  // commits never notified.
  ASSERT_TRUE(db_->Exec("SELECT pg_notify($1, 'marker')", {games_hub::RoomChannel("R1")}).ok());
  ASSERT_TRUE(received.Saw("marker"));
  const std::lock_guard<std::mutex> lock(received.mu);
  for (const std::string& payload : received.payloads) {
    EXPECT_NE(payload, "dupe");
    EXPECT_NE(payload, "v4");
  }
}

TEST_F(PgHubStoreTest, LoadGameReportsAVanishedGame) {
  auto row = store_->LoadGame("R1", "G1");
  ASSERT_TRUE(row.ok()) << row.status();
  EXPECT_FALSE(row->has_value());
}

// The finishing commit: final state, stat deltas, and the notify are
// one statement, so a replay (a retried statement after a lost answer)
// counts the game zero more times, not one.
TEST_F(PgHubStoreTest, FinishCommitAppliesStatsExactlyOnce) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"},
                   PgHubStore::UpsertMember{{"R1", "alice", true, 3, 1, 10}},
                   PgHubStore::UpsertMember{{"R1", "bob", true, 3, 0, 12}}});
  store_->Flush();
  auto started = store_->CommitGameSave({"R1", "G1", {"alice", "bob"}, std::nullopt, 1}, "start");
  ASSERT_TRUE(started.ok());
  ASSERT_TRUE(*started);

  const golf::GameState state = DealtState();
  const std::vector<PgHubStore::StatsDelta> deltas = {{"alice", 1, 1, 4}, {"bob", 1, 0, 9}};
  auto landed = store_->CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "over");
  ASSERT_TRUE(landed.ok()) << landed.status();
  EXPECT_TRUE(*landed);

  const auto expect_stats = [this] {
    auto room = store_->LoadRoom("R1");
    ASSERT_TRUE(room.ok()) << room.status();
    ASSERT_EQ(room->members.size(), 2u);
    for (const auto& member : room->members) {
      if (member.player_id == "alice") {
        EXPECT_EQ(member.games_played, 4);
        EXPECT_EQ(member.games_won, 2);
        EXPECT_EQ(member.total_score, 14);
      } else {
        EXPECT_EQ(member.player_id, "bob");
        EXPECT_EQ(member.games_played, 4);
        EXPECT_EQ(member.games_won, 0);
        EXPECT_EQ(member.total_score, 21);
      }
    }
  };
  expect_stats();

  // The ended row stays: remote instances read it for the game-over
  // ceremony, and room deletion owns its eventual cleanup.
  auto row = store_->LoadGame("R1", "G1");
  ASSERT_TRUE(row.ok());
  ASSERT_TRUE(row->has_value());
  EXPECT_EQ((*row)->version, 2);

  // The replay misses the version condition; nothing double-counts.
  landed = store_->CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "over");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  expect_stats();
}

TEST_F(PgHubStoreTest, LoadRoomScopesToOneRoom) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"},
                   PgHubStore::UpsertMember{{"R1", "alice", true, 0, 0, 0}},
                   PgHubStore::UpsertMember{{"R2", "carol", true, 0, 0, 0}}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, ""));
  ASSERT_TRUE(*store_->CommitGameSave({"R2", "G2", {"carol"}, std::nullopt, 1}, ""));

  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  EXPECT_TRUE(room->exists);
  ASSERT_EQ(room->members.size(), 1u);
  EXPECT_EQ(room->members[0].player_id, "alice");
  EXPECT_EQ(room->members[0].room_id, "R1");
  ASSERT_EQ(room->games.size(), 1u);
  EXPECT_EQ(room->games[0].game_id, "G1");

  auto missing = store_->LoadRoom("nope");
  ASSERT_TRUE(missing.ok());
  EXPECT_FALSE(missing->exists);
  EXPECT_TRUE(missing->members.empty());
  EXPECT_TRUE(missing->games.empty());
}

// The queued Notify op rides the FIFO: when it reaches a listener, the
// writes enqueued ahead of it have landed.
TEST_F(PgHubStoreTest, NotifyOpFiresAfterItsBatch) {
  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); },
      /*on_active=*/nullptr);
  listener.Listen(games_hub::kRoomsChannel);
  ConfirmSubscribed(games_hub::kRoomsChannel, received);

  store_->Enqueue(
      {PgHubStore::UpsertRoom{"R9"}, PgHubStore::Notify{games_hub::kRoomsChannel, "created R9"}});
  ASSERT_TRUE(received.Saw("created R9"));
  auto room = db_->Exec("SELECT 1 FROM rooms WHERE room_id = 'R9'");
  ASSERT_TRUE(room.ok());
  EXPECT_EQ(room->rows(), 1);
}

TEST_F(PgHubStoreTest, DeleteRoomCascades) {
  store_->Enqueue(
      {PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertMember{{"R1", "alice", true, 0, 0, 0}}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, ""));
  store_->Enqueue({PgHubStore::DeleteRoom{"R1"}});
  store_->Flush();
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->rooms.empty());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
}

}  // namespace
