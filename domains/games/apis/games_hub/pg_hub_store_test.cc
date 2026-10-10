#include "domains/games/apis/games_hub/pg_hub_store.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/migrations.h"
#include "domains/games/apis/games_hub/round_robin.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/castle/game_state_serde.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"
#include "domains/games/libs/cards/rummy/game_state.h"
#include "domains/games/libs/cards/rummy/table.h"
#include "domains/games/libs/cards/rummy/table_serde.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/game_state_serde.h"
#include "domains/games/libs/chess_play/table_serde.h"
#include "domains/platform/libs/pg/listener.h"
#include "domains/platform/libs/pg/pg.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using games_hub::Surface;
using ::testing::ElementsAre;

using games_hub::PgHubStore;

// A real engine state for the started-game rows — the store owns the
// serde end to end, so round-trip fidelity is asserted by canonical
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

// The persistence integration suite's write-through ops (#1194): the
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
    ASSERT_TRUE(db_->Exec("TRUNCATE rooms, published_chess_games CASCADE").ok());
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

// A chess table is the fourth: its kind is stored, and a table on its
// second game — a score line, moves and a running clock — decodes with
// the table's serde byte for byte.
TEST_F(PgHubStoreTest, ChessRowsKeepTheirKindAndDecodeWithChessSerde) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  auto opened = chess_play::Table::open(
      {"alice", "bob"}, "kpk", {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", 1}, {180'000, 2'000}, 1'000,
      std::string(chess_play::kRandomKpkSetup));
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto resigned = opened->inGame(
      [](const chess_play::GameState& game) { return game.resign(0, 2'000); });
  ASSERT_TRUE(resigned.ok()) << resigned.status();
  auto second = resigned->next("kpk", {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", 1},
                               {180'000, 2'000}, 3'000, std::string(chess_play::kRandomKpkSetup));
  ASSERT_TRUE(second.ok()) << second.status();
  auto moved = second->inGame(
      [](const chess_play::GameState& game) { return game.move(0, "e2e4", 5'000); });
  ASSERT_TRUE(moved.ok()) << moved.status();
  PgHubStore::GameRow row{
      "R1", "C1", {"alice", "bob"}, games_hub::HostedState(*moved), 1, games_hub::GameKind::kChess};
  ASSERT_TRUE(*store_->CommitGameSave(row, ""));

  auto reread = store_->LoadGame("R1", "C1");
  ASSERT_TRUE(reread.ok() && reread->has_value());
  EXPECT_EQ((*reread)->kind, games_hub::GameKind::kChess);
  ASSERT_TRUE((*reread)->state.has_value());
  ASSERT_TRUE(std::holds_alternative<chess_play::Table>(*(*reread)->state));
  EXPECT_EQ(chess_play::serializeTable(std::get<chess_play::Table>(*(*reread)->state)),
            chess_play::serializeTable(*moved));
}

// A challenge's terms (#1633) ride the state column while the table
// waits, and go when the game starts: the started row is the engine's
// alone.
TEST_F(PgHubStoreTest, ChallengeTermsRideTheStateColumnUntilTheStart) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  PgHubStore::GameRow waiting{"R1", "K1", {"alice"}, std::nullopt, 1, games_hub::GameKind::kChess};
  waiting.terms = games_hub::ChessTerms{"kpk-opposition", {60'000, 1'000}};
  ASSERT_TRUE(*store_->CommitGameSave(waiting, ""));
  auto reread = store_->LoadGame("R1", "K1");
  ASSERT_TRUE(reread.ok() && reread->has_value());
  EXPECT_FALSE((*reread)->state.has_value());
  ASSERT_TRUE((*reread)->terms.has_value());
  EXPECT_EQ((*reread)->terms->setup_id, "kpk-opposition");
  EXPECT_EQ((*reread)->terms->time_control, (chess_play::TimeControl{60'000, 1'000}));

  auto opened =
      chess_play::Table::open({"alice", "bob"}, "kpk", {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", 0},
                              {60'000, 1'000}, 1'000, "kpk-opposition");
  ASSERT_TRUE(opened.ok()) << opened.status();
  PgHubStore::GameRow started{"R1",
                              "K1",
                              {"alice", "bob"},
                              games_hub::HostedState(*opened),
                              2,
                              games_hub::GameKind::kChess};
  started.terms = waiting.terms;
  ASSERT_TRUE(*store_->CommitGameSave(started, ""));
  auto played = store_->LoadGame("R1", "K1");
  ASSERT_TRUE(played.ok() && played->has_value());
  EXPECT_TRUE((*played)->state.has_value());
  EXPECT_FALSE((*played)->terms.has_value());
}

// Stored terms that are not the terms shape lose the row, as any
// undecodable row does, rather than decoding as something else.
TEST_F(PgHubStoreTest, MalformedTermsLoseTheRow) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  PgHubStore::GameRow waiting{"R1", "K1", {"alice"}, std::nullopt, 1, games_hub::GameKind::kChess};
  ASSERT_TRUE(*store_->CommitGameSave(waiting, ""));
  for (const std::string& state :
       {R"({"terms":{"setupId":"standard","initialMs":"60000","incrementMs":0}})",
        R"({"terms":{"setupId":"standard","initialMs":60000}})", R"({"terms":"standard"})"}) {
    ASSERT_TRUE(db_->Exec("UPDATE games SET state = $1::jsonb WHERE game_id = 'K1'", {state}).ok());
    auto reread = store_->LoadGame("R1", "K1");
    ASSERT_TRUE(reread.ok()) << state;
    EXPECT_FALSE(reread->has_value()) << state;
  }
}

// The terms key is the codec's alone: no stored chess table has one at its
// top level, or a started row would read as a waiting challenge.
TEST_F(PgHubStoreTest, AStoredChessTableHasNoTopLevelTerms) {
  auto opened =
      chess_play::Table::open({"alice", "bob"}, "kpk", {"8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", 0},
                              {60'000, 1'000}, 1'000, "kpk-opposition");
  ASSERT_TRUE(opened.ok()) << opened.status();
  const auto encoded = nlohmann::json::parse(chess_play::serializeTable(*opened));
  ASSERT_TRUE(encoded.is_object());
  EXPECT_FALSE(encoded.contains("terms"));
}

// The control: a waiting chess table with no terms stores no state.
TEST_F(PgHubStoreTest, AWaitingTableWithNoTermsStoresNoState) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  PgHubStore::GameRow waiting{"R1", "K1", {"alice"}, std::nullopt, 1, games_hub::GameKind::kChess};
  ASSERT_TRUE(*store_->CommitGameSave(waiting, ""));
  auto reread = store_->LoadGame("R1", "K1");
  ASSERT_TRUE(reread.ok() && reread->has_value());
  EXPECT_FALSE((*reread)->state.has_value());
  EXPECT_FALSE((*reread)->terms.has_value());
}

// A rummy table (#245) is the third engine behind the same rows: its kind
// is stored, and a table mid-deal (#1609) — melds, the card taken from
// the discard, the last move — decodes with the table's serde byte for
// byte.
TEST_F(PgHubStoreTest, RummyRowsKeepTheirKindAndDecodeWithRummySerde) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  PgHubStore::GameRow waiting{"R1", "M1", {"alice"}, std::nullopt, 1, games_hub::GameKind::kRummy};
  ASSERT_TRUE(*store_->CommitGameSave(waiting, ""));
  cards::NoShuffleDealer dealer;
  auto dealt = rummy::dealRummyGame("M2", {"alice", "bob"}, dealer.DealNewUnshuffledDeck());
  ASSERT_TRUE(dealt.ok()) << dealt.status();
  auto drew = dealt->drawDiscard(0);
  ASSERT_TRUE(drew.ok());
  auto melded = drew->meld(0, {cards::Card{cards::Suit::Spades, cards::Rank::Ace},
                               cards::Card{cards::Suit::Spades, cards::Rank::King},
                               cards::Card{cards::Suit::Spades, cards::Rank::Queen}});
  ASSERT_TRUE(melded.ok()) << melded.status();
  const rummy::TableState table{
      {"alice", "bob"},          {0, 0},  1,  1, rummy::TablePhase::Playing,
      rummy::Variant::SevenCard, *melded, "", ""};
  PgHubStore::GameRow started{
      "R1", "M2", {"alice", "bob"}, games_hub::HostedState(table), 1, games_hub::GameKind::kRummy};
  ASSERT_TRUE(*store_->CommitGameSave(started, ""));

  auto rows = store_->LoadRoom("R1");
  ASSERT_TRUE(rows.ok()) << rows.status();
  ASSERT_EQ(rows->games.size(), 2u);
  for (const auto& game : rows->games) {
    EXPECT_EQ(game.kind, games_hub::GameKind::kRummy) << game.game_id;
    if (game.game_id == "M1") {
      EXPECT_FALSE(game.state.has_value());
      continue;
    }
    ASSERT_TRUE(game.state.has_value());
    ASSERT_TRUE(std::holds_alternative<rummy::TableState>(*game.state));
    EXPECT_EQ(rummy::serializeTableState(std::get<rummy::TableState>(*game.state)),
              rummy::serializeTableState(table));
  }
  // A started row's column follows its state, whatever the row said.
  PgHubStore::GameRow mislabeled{"R1", "M3", {"alice", "bob"}, games_hub::HostedState(table), 1};
  ASSERT_TRUE(*store_->CommitGameSave(mislabeled, ""));
  auto relabeled = store_->LoadGame("R1", "M3");
  ASSERT_TRUE(relabeled.ok() && relabeled->has_value());
  EXPECT_EQ((*relabeled)->kind, games_hub::GameKind::kRummy);
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
  EXPECT_EQ(snapshot->rooms[0].room_id, "R1");
  EXPECT_EQ(snapshot->rooms[0].surface, Surface::Plane());
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

  // Upserts converge on the latest presence and leave the stats the row
  // was made with (only a finish's increments move those); deletes remove
  // exactly their row.
  alice.total_score = 12;
  alice.connected = false;
  store_->Enqueue({PgHubStore::UpsertMember{alice}, PgHubStore::DeleteMember{"R1", "bob"},
                   PgHubStore::DeleteGame{"R1", "G1"}});
  store_->Flush();
  snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->members.size(), 1u);
  EXPECT_EQ(snapshot->members[0].total_score, 9);
  EXPECT_FALSE(snapshot->members[0].connected);
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].game_id, "G2");
}

// The commit path (#1194): the notify must ride exactly the
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

// Stats move only by a finish's increments. A presence write from an
// instance that has not yet heard of a finish carries stale stats, and a
// rummy table finishes a deal every hand (#1609): the write updates
// presence and leaves the counts to the increments that own them.
TEST_F(PgHubStoreTest, PresenceWritesLeaveStatsToTheirIncrements) {
  store_->Enqueue(
      {PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertMember{{"R1", "alice", true, 3, 1, 10}}});
  store_->Flush();
  auto started = store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, "start");
  ASSERT_TRUE(started.ok() && *started);
  auto landed = store_->CommitGameFinish({"R1", "G1", {"alice"}, DealtState(), 2},
                                         {{"alice", 1, 1, 4}}, "over");
  ASSERT_TRUE(landed.ok() && *landed) << landed.status();

  // The sibling's stale view of alice: her stats from before the finish.
  store_->Enqueue({PgHubStore::UpsertMember{{"R1", "alice", false, 3, 1, 10}}});
  store_->Flush();
  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  ASSERT_EQ(room->members.size(), 1u);
  EXPECT_FALSE(room->members[0].connected);
  EXPECT_EQ(room->members[0].games_played, 4);
  EXPECT_EQ(room->members[0].games_won, 2);
  EXPECT_EQ(room->members[0].total_score, 14);
}

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";

chess_play::Table ChessOpened(std::vector<std::string> players = {"alice", "bob"},
                              int white_seat = 0) {
  auto table =
      chess_play::Table::open(std::move(players), "kpk", chess_play::Opening{kMate, white_seat},
                              {180'000, 2'000}, 1'000, std::string(chess_play::kRandomKpkSetup));
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

chess_play::Table Mated(const chess_play::Table& table) {
  auto mated = table.inGame([&](const chess_play::GameState& game) {
    return game.move(game.whiteSeat(), "e7e8q", 2'000);
  });
  EXPECT_TRUE(mated.ok()) << mated.status();
  return *mated;
}

chess_play::Table Next(const chess_play::Table& table) {
  auto next = table.next("kpk", chess_play::Opening{kMate, 0}, {180'000, 2'000}, 3'000,
                         std::string(chess_play::kRandomKpkSetup));
  EXPECT_TRUE(next.ok()) << next.status();
  return *next;
}

PgHubStore::GameRow ChessRow(const chess_play::Table& table, int64_t version) {
  return {"R1",
          "C1",
          table.players(),
          games_hub::HostedState(table),
          version,
          games_hub::GameKind::kChess};
}

// The commit that ends a chess game archives it in the same statement
// (#1637): once, whichever later commits still carry it, never from a
// commit that missed, and marked with the room's published flag of the
// moment.
TEST_F(PgHubStoreTest, ACommitThatEndsAChessGameArchivesItOnce) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  const chess_play::Table opened = ChessOpened();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(opened, 1), ""));
  auto history = store_->LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok()) << history.status();
  EXPECT_FALSE(history->published);
  EXPECT_TRUE(history->games.empty());

  const chess_play::Table mated = Mated(opened);
  EXPECT_FALSE(*store_->CommitGameSave(ChessRow(mated, 3), "")) << "a miss";
  EXPECT_FALSE(*store_->CommitGameFinish(ChessRow(mated, 3), {}, "")) << "a miss";
  EXPECT_TRUE(store_->LoadChessHistory("R1", 100)->games.empty()) << "misses archive nothing";

  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(mated, 2), ""));
  ASSERT_TRUE(*store_->CommitGameFinish(ChessRow(*mated.removePlayer(1, 2'500), 3), {}, ""));
  history = store_->LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok()) << history.status();
  ASSERT_EQ(history->games.size(), 1u);
  EXPECT_EQ(history->games[0].game_id, "C1");
  EXPECT_EQ(history->games[0].ordinal, 1);
  EXPECT_EQ(chess_play::serializeGameState(history->games[0].game),
            chess_play::serializeGameState(mated.game()));
  EXPECT_FALSE(history->games[0].published);
  EXPECT_GT(history->games[0].ended_at_ms, 1'700'000'000'000) << "a wall-clock stamp";
}

TEST_F(PgHubStoreTest, ChessHistoryIsNewestFirstAndTheFeedIsWhatEndedPublished) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"}});
  store_->Flush();
  chess_play::Table table = ChessOpened();
  int64_t version = 1;
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(table, version++), ""));
  table = Mated(table);
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(table, version++), ""));  // private
  EXPECT_TRUE(store_->LoadPublishedChess(0, 100)->empty());
  store_->Enqueue(
      {PgHubStore::SetChessPublished{"R1", true}, PgHubStore::SetChessPublished{"ghost", true}});
  store_->Flush();
  for (int game = 2; game <= 4; ++game) {
    table = Next(table);
    ASSERT_TRUE(*store_->CommitGameSave(ChessRow(table, version++), ""));
    table = Mated(table);
    ASSERT_TRUE(*store_->CommitGameSave(ChessRow(table, version++), ""));
  }
  // The close carries game 4 again: the feed takes it once.
  ASSERT_TRUE(
      *store_->CommitGameFinish(ChessRow(*table.removePlayer(1, 9'000), version++), {}, ""));
  store_->Enqueue({PgHubStore::SetChessPublished{"R1", false}});
  store_->Flush();

  auto history = store_->LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok()) << history.status();
  EXPECT_FALSE(history->published);
  ASSERT_EQ(history->games.size(), 4u);
  EXPECT_EQ(history->games[0].ordinal, 4);
  EXPECT_GT(history->games[0].archive_id, history->games[1].archive_id);
  EXPECT_EQ(history->games[3].ordinal, 1);
  EXPECT_FALSE(history->games[3].published);
  EXPECT_TRUE(history->games[0].published);
  EXPECT_EQ(store_->LoadChessHistory("R1", 2)->games.size(), 2u);
  EXPECT_TRUE(store_->LoadChessHistory("R2", 100)->games.empty()) << "scoped to the room";
  EXPECT_EQ(store_->LoadChessHistory("ghost", 100).status().code(), absl::StatusCode::kNotFound);

  auto feed = store_->LoadPublishedChess(0, 100);
  ASSERT_TRUE(feed.ok()) << feed.status();
  ASSERT_EQ(feed->size(), 3u) << "withdrawing keeps what was already out";
  EXPECT_EQ((*feed)[0].archive_id, history->games[2].archive_id);
  EXPECT_EQ((*feed)[2].archive_id, history->games[0].archive_id);
  EXPECT_EQ((*feed)[2].ended_at_ms, history->games[0].ended_at_ms);
  EXPECT_EQ(chess_play::serializeGameState((*feed)[2].game),
            chess_play::serializeGameState(history->games[0].game));
  feed = store_->LoadPublishedChess((*feed)[0].archive_id, 100);
  ASSERT_TRUE(feed.ok());
  EXPECT_EQ(feed->size(), 2u) << "after is exclusive";
  EXPECT_EQ(store_->LoadPublishedChess(0, 1)->size(), 1u);

  EXPECT_TRUE(store_->LoadRoom("R1").ok());
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  for (const auto& room : snapshot->rooms) EXPECT_FALSE(room.chess_published) << room.room_id;

  store_->Enqueue({PgHubStore::DeleteRoom{"R1"}});
  store_->Flush();
  auto left = db_->Exec("SELECT count(*) FROM chess_games");
  ASSERT_TRUE(left.ok());
  EXPECT_EQ(left->Get(0, 0).value_or(""), "0") << "the room's archive dies with it";
  EXPECT_EQ(store_->LoadPublishedChess(0, 100)->size(), 3u) << "the feed does not";
}

TEST_F(PgHubStoreTest, AnArchivedGameIsFoundByIdOrByTableAndLine) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  store_->Enqueue({PgHubStore::DeleteGame{"R1", "C1"}});
  store_->Flush();
  const chess_play::Table other = ChessOpened({"carol", "dave"});
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(other, 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(other), 2), ""));
  auto history = store_->LoadChessHistory("R1", 100);
  ASSERT_EQ(history->games.size(), 2u);
  const int64_t older = history->games[1].archive_id;
  const int64_t newer = history->games[0].archive_id;

  auto found = store_->LoadChessGame("R1", {older, "", 0});
  ASSERT_TRUE(found.ok() && found->has_value()) << found.status();
  EXPECT_EQ((*found)->archive_id, older);
  EXPECT_EQ((*found)->game.players(), (std::vector<std::string>{"alice", "bob"}));
  found = store_->LoadChessGame("R1", {std::nullopt, "C1", 1});
  ASSERT_TRUE(found.ok() && found->has_value());
  EXPECT_EQ((*found)->archive_id, newer);
  EXPECT_FALSE(store_->LoadChessGame("R1", {std::nullopt, "C1", 2})->has_value());
  EXPECT_FALSE(store_->LoadChessGame("R2", {older, "", 0})->has_value()) << "another room's";
}

// Retention: the feed is swept by age, the room's own archive is not.
TEST_F(PgHubStoreTest, ThePublishedFeedIsSweptByAge) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::SetChessPublished{"R1", true}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  store_->Enqueue({PgHubStore::SweepPublishedChess{std::chrono::hours(1)}});
  store_->Flush();
  EXPECT_EQ(store_->LoadPublishedChess(0, 100)->size(), 1u) << "fresh stays";
  ASSERT_TRUE(
      db_->Exec("UPDATE published_chess_games SET ended_at = now() - interval '2 hours'").ok());
  store_->Enqueue({PgHubStore::SweepPublishedChess{std::chrono::hours(1)}});
  store_->Flush();
  EXPECT_TRUE(store_->LoadPublishedChess(0, 100)->empty());
  EXPECT_EQ(store_->LoadChessHistory("R1", 100)->games.size(), 1u);
}

// An unreadable row in the feed costs that row, never the rows after it:
// a page reads on past it, so a reader advancing by the last id it got
// cannot stall on a page the bad rows emptied.
TEST_F(PgHubStoreTest, TheFeedReadsOnPastUnreadableRows) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::SetChessPublished{"R1", true}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  auto feed = store_->LoadPublishedChess(0, 100);
  ASSERT_TRUE(feed.ok() && feed->size() == 1u);
  const int64_t good = (*feed)[0].archive_id;
  // Three unreadable rows past it, then a readable one.
  for (int64_t bad = good + 1; bad <= good + 3; ++bad) {
    ASSERT_TRUE(db_->Exec("INSERT INTO published_chess_games (archive_id, game, ended_at)"
                          " VALUES ($1::bigint, '{\"v\":99}', now())",
                          {std::to_string(bad)})
                    .ok());
  }
  ASSERT_TRUE(db_->Exec("INSERT INTO published_chess_games (archive_id, game, ended_at)"
                        " SELECT $1::bigint, game, ended_at FROM published_chess_games"
                        " WHERE archive_id = $2::bigint",
                        {std::to_string(good + 4), std::to_string(good)})
                  .ok());

  feed = store_->LoadPublishedChess(good, 2);
  ASSERT_TRUE(feed.ok()) << feed.status();
  ASSERT_EQ(feed->size(), 1u) << "the bad rows filled the first SQL page";
  EXPECT_EQ((*feed)[0].archive_id, good + 4);
  feed = store_->LoadPublishedChess(good + 4, 2);
  ASSERT_TRUE(feed.ok());
  EXPECT_TRUE(feed->empty()) << "and the end of the feed is still the end";
}

// Milliseconds are truncated: a game that ended at .6ms reads as the ms
// it ended in, never the next.
TEST_F(PgHubStoreTest, EndTimesTruncateToTheMillisecond) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::SetChessPublished{"R1", true}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  ASSERT_TRUE(db_->Exec("UPDATE chess_games SET ended_at = to_timestamp(1.0006)").ok());
  ASSERT_TRUE(db_->Exec("UPDATE published_chess_games SET ended_at = to_timestamp(1.0006)").ok());
  EXPECT_EQ(store_->LoadChessHistory("R1", 100)->games[0].ended_at_ms, 1'000);
  EXPECT_EQ((*store_->LoadPublishedChess(0, 100))[0].ended_at_ms, 1'000);
}

// A table code freed and minted again replays ordinals under the same
// game id; those are other games, and kept.
TEST_F(PgHubStoreTest, AReMintedTableCodeArchivesItsOwnGames) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  store_->Enqueue({PgHubStore::DeleteGame{"R1", "C1"}});
  store_->Flush();
  const chess_play::Table other = ChessOpened({"carol", "dave"});
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(other, 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(other), 2), ""));
  EXPECT_EQ(store_->LoadChessHistory("R1", 100)->games.size(), 2u);
}

// An archived game that no longer restores costs that game, not the
// room's history.
TEST_F(PgHubStoreTest, AnUnreadableArchivedGameIsDropped) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store_->CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  ASSERT_TRUE(db_->Exec("INSERT INTO chess_games (room_id, game_id, ordinal, game, published)"
                        " VALUES ('R1', 'C9', 1, '{\"v\":99}', false)")
                  .ok());
  auto history = store_->LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok()) << history.status();
  ASSERT_EQ(history->games.size(), 1u);
  EXPECT_EQ(history->games[0].game_id, "C1");
}

// The surface a room stands on rides its row (#1554): stored in the
// wire's spelling, set by the create, kept against a second create,
// changed by SetRoomSurface (a no-op for a room that is not), and read
// back by both loads. A row from before the column (the migration's
// default) and a row whose geometry nothing here can read are the
// plane, so a bad row costs the room its shape and never the boot.
TEST_F(PgHubStoreTest, RoomGeometryRoundTripsAndUnreadableRowsAreFlat) {
  store_->Enqueue({PgHubStore::UpsertRoom{"S", Surface::Sphere(2)}});
  store_->Flush();
  store_->Enqueue({PgHubStore::UpsertRoom{"S", Surface::Plane()},
                   PgHubStore::SetRoomSurface{"S", Surface::Sphere(53)},
                   PgHubStore::SetRoomSurface{"ghost", Surface::Sphere(53)}});
  store_->Flush();
  EXPECT_FALSE(store_->LoadRoom("ghost")->exists);
  ASSERT_TRUE(db_->Exec("INSERT INTO rooms (room_id) VALUES ('P')").ok());
  ASSERT_TRUE(
      db_->Exec(R"(INSERT INTO rooms (room_id, geometry) VALUES ('T', '{"torus":{}}'))").ok());
  auto stored = db_->Exec("SELECT geometry::text FROM rooms WHERE room_id = 'S'");
  ASSERT_TRUE(stored.ok());
  EXPECT_EQ(stored->Get(0, 0).value_or(""), R"({"sphere": {"radius": 53.0}})");

  auto sphere = store_->LoadRoom("S");
  ASSERT_TRUE(sphere.ok()) << sphere.status();
  EXPECT_EQ(sphere->surface, Surface::Sphere(53));
  auto plane = store_->LoadRoom("P");
  ASSERT_TRUE(plane.ok()) << plane.status();
  EXPECT_EQ(plane->surface, Surface::Plane());
  auto torus = store_->LoadRoom("T");
  ASSERT_TRUE(torus.ok()) << torus.status();
  EXPECT_EQ(torus->surface, Surface::Plane());

  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  ASSERT_EQ(snapshot->rooms.size(), 3u);
  for (const auto& room : snapshot->rooms) {
    EXPECT_EQ(room.surface, room.room_id == "S" ? Surface::Sphere(53) : Surface::Plane())
        << room.room_id;
  }
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

// last_active_at is what the room sweep reads: every write that names a
// room marks it active, and only that room. A crashed instance's ghost
// room is the one nobody writes to, so its stamp is the one that ages.
TEST_F(PgHubStoreTest, EveryWriteNamingARoomMarksItActive) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"},
                   PgHubStore::UpsertMember{{"R1", "alice", true, 0, 0, 0}},
                   PgHubStore::UpsertMember{{"R1", "bob", true, 0, 0, 0}}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, ""));

  const auto age_both = [this] {
    ASSERT_TRUE(db_->Exec("UPDATE rooms SET last_active_at = '2000-01-01Z'").ok());
  };
  const auto is_fresh = [this](const std::string& room_id) {
    auto result = db_->Exec(
        "SELECT 1 FROM rooms WHERE room_id = $1 AND last_active_at > '2000-01-01Z'", {room_id});
    EXPECT_TRUE(result.ok()) << result.status();
    return result.ok() && result->rows() == 1;
  };
  const auto enqueued = [this](PgHubStore::Op op) {
    return [this, op] {
      store_->Enqueue({op});
      store_->Flush();
    };
  };

  const golf::GameState state = DealtState();
  const std::vector<std::pair<std::string, std::function<void()>>> writes = {
      {"UpsertRoom", enqueued(PgHubStore::UpsertRoom{"R1"})},
      {"SetRoomSurface", enqueued(PgHubStore::SetRoomSurface{"R1", Surface::Sphere(53)})},
      {"UpsertMember", enqueued(PgHubStore::UpsertMember{{"R1", "alice", false, 0, 0, 0}})},
      {"DeleteMember", enqueued(PgHubStore::DeleteMember{"R1", "bob"})},
      {"CommitGameSave",
       [this] {
         ASSERT_TRUE(*store_->CommitGameSave({"R1", "G2", {"alice"}, std::nullopt, 1}, ""));
       }},
      {"CommitGameFinish",
       [&] { ASSERT_TRUE(*store_->CommitGameFinish({"R1", "G1", {"alice"}, state, 2}, {}, "")); }},
      {"DeleteGame", enqueued(PgHubStore::DeleteGame{"R1", "G2"})},
  };
  for (const auto& [name, write] : writes) {
    age_both();
    write();
    EXPECT_TRUE(is_fresh("R1")) << name << " left its room's stamp stale";
    EXPECT_FALSE(is_fresh("R2")) << name << " touched a room it does not name";
  }
}

// The heartbeat's write: one batch stamps exactly the rooms it names,
// and a name with no row is no error.
TEST_F(PgHubStoreTest, TouchRoomsStampsExactlyTheNamedRooms) {
  store_->Enqueue(
      {PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"}, PgHubStore::UpsertRoom{"R3"}});
  store_->Flush();
  ASSERT_TRUE(db_->Exec("UPDATE rooms SET last_active_at = '2000-01-01Z'").ok());
  store_->Enqueue({PgHubStore::TouchRooms{{"R1", "R3", "gone"}}});
  store_->Flush();
  auto fresh =
      db_->Exec("SELECT room_id FROM rooms WHERE last_active_at > '2000-01-01Z' ORDER BY room_id");
  ASSERT_TRUE(fresh.ok()) << fresh.status();
  ASSERT_EQ(fresh->rows(), 2);
  EXPECT_EQ(fresh->Get(0, 0).value_or(""), "R1");
  EXPECT_EQ(fresh->Get(1, 0).value_or(""), "R3");
}

// The sweep: a room whose stamp is older than the threshold goes, with
// everything that cascades from it, and its channel hears a wake so any
// instance still holding it drops it too. A fresh room stays.
TEST_F(PgHubStoreTest, SweepDeletesStaleRoomsAndWakesTheirHolders) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}, PgHubStore::UpsertRoom{"R2"},
                   PgHubStore::UpsertMember{{"R1", "alice", true, 0, 0, 0}}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, ""));
  ASSERT_TRUE(
      db_->Exec("UPDATE rooms SET last_active_at = now() - interval '2 hours' WHERE room_id = 'R1'")
          .ok());

  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); },
      /*on_active=*/nullptr);
  listener.Listen(games_hub::RoomChannel("R1"));
  ConfirmSubscribed(games_hub::RoomChannel("R1"), received);

  store_->Enqueue({PgHubStore::SweepRooms{std::chrono::hours(1)}});
  store_->Flush();

  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  ASSERT_EQ(snapshot->rooms.size(), 1u);
  EXPECT_EQ(snapshot->rooms[0].room_id, "R2");
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
  EXPECT_TRUE(received.Saw(games_hub::kSweepWake)) << "the swept room's holders were never woken";
}

// A new room starts active.
TEST_F(PgHubStoreTest, NewRoomsStartActive) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  auto result = db_->Exec(
      "SELECT 1 FROM rooms WHERE room_id = 'R1' AND last_active_at > now() - interval '1 minute'");
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->rows(), 1);
}

PgHubStore::ChessEventRow Event(int64_t version, const std::string& creator = "alice") {
  PgHubStore::ChessEventRow row;
  row.room_id = "R1";
  row.event_id = "E1";
  row.version = version;
  row.creator = creator;
  row.entrants = {"alice", "bob", "carol"};
  row.terms = {"standard", {180'000, 2'000}};
  row.pairings = *games_hub::RoundRobinPairings(row.entrants);
  return row;
}

// A round robin (#1647) commits as a games row does, notifying exactly
// the commits that land; its body round-trips through both loads, and
// the room's cascade takes it.
TEST_F(PgHubStoreTest, AnEventCommitsOnItsVersionNotifiesAndDiesWithItsRoom) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  Received received;
  pg::Listener listener(
      url_, [&](const std::string&, const std::string& payload) { received.Add(payload); },
      /*on_active=*/nullptr);
  listener.Listen(games_hub::RoomChannel("R1"));
  ConfirmSubscribed(games_hub::RoomChannel("R1"), received);

  ASSERT_TRUE(*store_->CommitChessEvent(Event(1), "v1"));
  EXPECT_TRUE(received.Saw("v1"));
  EXPECT_FALSE(*store_->CommitChessEvent(Event(1, "mallory"), "dupe")) << "the id is taken";
  PgHubStore::ChessEventRow ghost = Event(1);
  ghost.room_id = "ghost";
  auto refused = store_->CommitChessEvent(ghost, "ghost");
  ASSERT_TRUE(refused.ok()) << refused.status();
  EXPECT_FALSE(*refused) << "no such room";

  PgHubStore::ChessEventRow moderated = Event(2);
  moderated.withdrawn = {"carol"};
  moderated.pairings[0].result = games_hub::PairingResult::kWhite;
  moderated.pairings[0].forfeit = true;
  moderated.pairings[1].result = games_hub::PairingResult::kDraw;
  moderated.pairings[2].result = games_hub::PairingResult::kBlack;
  moderated.pairings[2].forfeit = true;
  EXPECT_FALSE(*store_->CommitChessEvent(Event(3), "v3")) << "skips a version";
  ASSERT_TRUE(*store_->CommitChessEvent(moderated, "v2"));
  EXPECT_TRUE(received.Saw("v2"));
  EXPECT_FALSE(*store_->CommitChessEvent(Event(2), "stale")) << "replaces 1, not 2";

  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  ASSERT_EQ(room->events.size(), 1u);
  const PgHubStore::ChessEventRow& loaded = room->events[0];
  EXPECT_EQ(loaded.room_id, "R1");
  EXPECT_EQ(loaded.event_id, "E1");
  EXPECT_EQ(loaded.version, 2);
  EXPECT_EQ(loaded.creator, "alice");
  EXPECT_EQ(loaded.entrants, moderated.entrants);
  EXPECT_EQ(loaded.terms, moderated.terms);
  EXPECT_EQ(loaded.pairings, moderated.pairings);
  EXPECT_EQ(loaded.withdrawn, moderated.withdrawn);
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  ASSERT_EQ(snapshot->events.size(), 1u);
  EXPECT_EQ(snapshot->events[0].pairings, moderated.pairings);

  ASSERT_TRUE(db_->Exec("SELECT pg_notify($1, 'marker')", {games_hub::RoomChannel("R1")}).ok());
  ASSERT_TRUE(received.Saw("marker"));
  {
    const std::lock_guard<std::mutex> lock(received.mu);
    for (const std::string& payload : received.payloads) {
      EXPECT_NE(payload, "dupe");
      EXPECT_NE(payload, "v3");
      EXPECT_NE(payload, "stale");
    }
  }

  store_->Enqueue({PgHubStore::DeleteRoom{"R1"}});
  store_->Flush();
  EXPECT_TRUE(store_->LoadSnapshot()->events.empty());
}

// White to move; b5b6 stalemates the king on a8.
constexpr char kStalemate[] = "k7/8/2K5/1Q6/8/8/8/8 w - - 0 1";

chess_play::Table Drawn() {
  auto table =
      chess_play::Table::open({"alice", "bob"}, "kpk", chess_play::Opening{kStalemate, 0},
                              {180'000, 2'000}, 1'000, std::string(chess_play::kRandomKpkSetup));
  EXPECT_TRUE(table.ok()) << table.status();
  auto drawn = table->inGame([&](const chess_play::GameState& game) {
    return game.move(game.whiteSeat(), "b5b6", 2'000);
  });
  EXPECT_TRUE(drawn.ok()) << drawn.status();
  EXPECT_TRUE(drawn->game().isOver());
  return *drawn;
}

PgHubStore::GameRow Tagged(const chess_play::Table& table, int64_t version,
                           const std::string& game_id, std::optional<games_hub::EventTag> tag,
                           const std::string& room_id = "R1") {
  PgHubStore::GameRow row = ChessRow(table, version);
  row.room_id = room_id;
  row.game_id = game_id;
  row.event = std::move(tag);
  return row;
}

// The commit that ends a tagged table's first game archives it as its
// pairing's, in the same statement, through the update and the finish
// alike: the event lists it with its winner (none for a draw) under the
// tag the table was made with. A rematch on the table is not the
// pairing's, and an untagged table's game is history, not the event's.
TEST_F(PgHubStoreTest, ATaggedTablesFirstGameIsArchivedAsItsPairings) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitChessEvent(Event(1), ""));
  const games_hub::EventTag tag{"E1", 2};
  // White in the second seat: the winner is named by seat, not by order.
  const chess_play::Table opened = ChessOpened({"alice", "bob"}, 1);
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(opened, 1, "C1", tag), ""));
  EXPECT_EQ((*store_->LoadGame("R1", "C1"))->event, std::optional<games_hub::EventTag>(tag));

  const chess_play::Table mated = Mated(opened);
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(mated, 2, "C1", games_hub::EventTag{"E9", 0}), ""));
  EXPECT_EQ((*store_->LoadGame("R1", "C1"))->event, std::optional<games_hub::EventTag>(tag))
      << "only the insert writes the tag";
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(Next(mated), 3, "C1", std::nullopt), ""));
  const chess_play::Table rematched = Mated(Next(mated));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(rematched, 4, "C1", std::nullopt), ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(Next(rematched), 5, "C1", std::nullopt), ""));
  ASSERT_TRUE(
      *store_->CommitGameFinish(Tagged(Mated(Next(rematched)), 6, "C1", std::nullopt), {}, ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(opened, 1, "C2", std::nullopt), ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(mated, 2, "C2", std::nullopt), ""));
  const chess_play::Table drawn = Drawn();
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(drawn, 1, "C3", games_hub::EventTag{"E1", 0}), ""));
  ASSERT_TRUE(*store_->CommitGameFinish(
      Tagged(*drawn.removePlayer(0, 3'000), 2, "C3", std::nullopt), {}, ""));

  ASSERT_EQ(store_->LoadChessHistory("R1", 100)->games.size(), 5u);
  const auto archived = [&](const std::string& game_id, int ordinal) {
    return (*store_->LoadChessGame("R1", {std::nullopt, game_id, ordinal}))->archive_id;
  };
  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  ASSERT_EQ(room->events.size(), 1u);
  ASSERT_EQ(opened.players()[opened.game().whiteSeat()], "bob");
  EXPECT_THAT(room->events[0].games,
              ElementsAre(PgHubStore::EventGame{archived("C1", 1), "C1", 2, "bob"},
                          PgHubStore::EventGame{archived("C3", 1), "C3", 0, std::nullopt}))
      << "archive order";

  // Each event holds its own games: another event in the room, and one
  // of the same id in another room, each with a game of its own.
  store_->Enqueue({PgHubStore::UpsertRoom{"R2"}});
  store_->Flush();
  PgHubStore::ChessEventRow second = Event(1);
  second.event_id = "E2";
  PgHubStore::ChessEventRow elsewhere = Event(1);
  elsewhere.room_id = "R2";
  ASSERT_TRUE(*store_->CommitChessEvent(second, ""));
  ASSERT_TRUE(*store_->CommitChessEvent(elsewhere, ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(opened, 1, "C4", games_hub::EventTag{"E2", 1}), ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(mated, 2, "C4", std::nullopt), ""));
  ASSERT_TRUE(
      *store_->CommitGameSave(Tagged(opened, 1, "C1", games_hub::EventTag{"E1", 1}, "R2"), ""));
  ASSERT_TRUE(*store_->CommitGameSave(Tagged(mated, 2, "C1", std::nullopt, "R2"), ""));

  // An event loaded with its games and committed back keeps them as the
  // archive has them: the commit writes none.
  PgHubStore::ChessEventRow reloaded = store_->LoadRoom("R1")->events[0];
  reloaded.version = 2;
  ASSERT_TRUE(*store_->CommitChessEvent(reloaded, ""));

  room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  ASSERT_EQ(room->events.size(), 2u);
  EXPECT_EQ(room->events[0].games.size(), 2u);
  ASSERT_EQ(room->events[1].games.size(), 1u);
  EXPECT_EQ(room->events[1].games[0].game_id, "C4");
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  ASSERT_EQ(snapshot->events.size(), 3u);
  for (const PgHubStore::ChessEventRow& event : snapshot->events) {
    const size_t want = event.room_id == "R1" && event.event_id == "E1" ? 2u : 1u;
    EXPECT_EQ(event.games.size(), want) << event.room_id << "/" << event.event_id;
  }
  EXPECT_EQ(snapshot->events[0].games, room->events[0].games);
}

// An event body nothing here can read costs that event, logged, as an
// unreadable games row costs its game: never the room or the boot.
TEST_F(PgHubStoreTest, AnUnreadableEventIsDropped) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitChessEvent(Event(1), ""));
  ASSERT_TRUE(db_->Exec("INSERT INTO chess_events (room_id, event_id, version, body)"
                        " VALUES ('R1', 'E9', 1, '{\"v\":99}')")
                  .ok());
  // Bodies that read as events until a pairing doesn't.
  const nlohmann::json good = nlohmann::json::parse(
      *db_->Exec("SELECT body::text FROM chess_events WHERE event_id = 'E1'")->Get(0, 0));
  int n = 0;
  for (const auto& [key, value] :
       std::vector<std::pair<std::string, nlohmann::json>>{{"result", "white wins"},
                                                           {"result", 1},
                                                           {"forfeit", "yes"},
                                                           {"round", "one"},
                                                           {"white", nullptr}}) {
    nlohmann::json bad = good;
    bad["pairings"][0][key] = value;
    ASSERT_TRUE(db_->Exec("INSERT INTO chess_events (room_id, event_id, version, body)"
                          " VALUES ('R1', $1, 1, $2::jsonb)",
                          {"B" + std::to_string(++n), bad.dump()})
                    .ok());
  }
  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  ASSERT_EQ(room->events.size(), 1u);
  EXPECT_EQ(room->events[0].event_id, "E1");
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_EQ(snapshot->events.size(), 1u);
}

// Two tables racing to play one pairing each end a first game, and the
// event lists both, in archive order: the store does not pick.
TEST_F(PgHubStoreTest, TwoTablesPlayingOnePairingAreBothListed) {
  store_->Enqueue({PgHubStore::UpsertRoom{"R1"}});
  store_->Flush();
  ASSERT_TRUE(*store_->CommitChessEvent(Event(1), ""));
  const chess_play::Table opened = ChessOpened();
  for (const std::string game_id : {"C1", "C2"}) {
    ASSERT_TRUE(
        *store_->CommitGameSave(Tagged(opened, 1, game_id, games_hub::EventTag{"E1", 1}), ""));
    ASSERT_TRUE(*store_->CommitGameSave(Tagged(Mated(opened), 2, game_id, std::nullopt), ""));
  }
  auto room = store_->LoadRoom("R1");
  ASSERT_TRUE(room.ok()) << room.status();
  const auto& games = room->events[0].games;
  ASSERT_EQ(games.size(), 2u);
  EXPECT_EQ(games[0].game_id, "C1");
  EXPECT_EQ(games[1].game_id, "C2");
  EXPECT_EQ(games[0].pairing, 1);
  EXPECT_EQ(games[1].pairing, 1);
  EXPECT_LT(games[0].archive_id, games[1].archive_id);
}

}  // namespace
