// The step-2/3 persistence e2e (#1194): the same client flows as
// hub_e2e_test, but over durable credentials (step 1) and the rooms/games
// write-through — then the process "dies" (RestartHub) and a fresh hub
// over the same database has to seat everyone back into their live game.
// The step-3 suite adds a second live instance (BuildInstance): one room
// and one game shared across two hubs whose only channel is the database
// and its NOTIFY wire. Real postgres via GOLF_HUB_TEST_DB_URL; skips
// otherwise.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "domains/games/apis/golf_hub/migrations.h"
#include "domains/games/apis/golf_hub/pg_hub_store.h"
#include "domains/games/apis/golf_hub/pg_ticket_vault.h"
#include "domains/games/apis/golf_hub/stream_test_fixture.h"
#include "domains/platform/libs/pg/listener.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfMove;

// Receives roomState frames until one satisfies the predicate — the
// cross-instance tests converge on content, not frame counts, because a
// wake re-projects everything and the frame count is timing-dependent.
template <typename Predicate>
std::optional<moonbase::golf::RoomState> AwaitRoomState(moonbase::golf::PlayClientStream& stream,
                                                        Predicate&& predicate) {
  for (int i = 0; i < 32; ++i) {
    auto event = ReceiveCase(stream, "roomState");
    if (!event.has_value()) return std::nullopt;
    const auto* room = event->as_roomState_or_null();
    if (predicate(*room)) return *room;
  }
  return std::nullopt;
}

template <typename Predicate>
std::optional<moonbase::golf::GameView> AwaitGameView(moonbase::golf::PlayClientStream& stream,
                                                      Predicate&& predicate) {
  for (int i = 0; i < 32; ++i) {
    auto update = ReceiveGolf(stream, "gameState");
    if (!update.has_value()) return std::nullopt;
    const auto& view = update->as_gameState_or_null()->view;
    if (predicate(view)) return view;
  }
  return std::nullopt;
}

// Distinct id space for the second instance: two SequentialIdGenerators
// would both mint "player-1", and the shared database would treat the
// two seats as one person.
class RemoteIdGenerator final : public IdGenerator {
 public:
  std::string PlayerId() override { return "remote-player-" + std::to_string(++players_); }
  std::string RoomId() override { return "remote-room-" + std::to_string(++rooms_); }
  std::string GameCode() override { return "RGAME" + std::to_string(++games_); }

 private:
  int players_ = 0;
  int rooms_ = 0;
  int games_ = 0;
};

class PgGolfHubFixture : public GolfHubStreamFixture {
 protected:
  void SetUp() override {
    url_ = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    pg::Client db(url_);
    ASSERT_TRUE(RunMigrations(db).ok());
    ASSERT_TRUE(db.Exec("TRUNCATE rooms CASCADE").ok());
    ASSERT_TRUE(db.Exec("TRUNCATE tickets, resume_tokens").ok());
    GolfHubStreamFixture::SetUp();
    listener_ = MakeListener(handler_);
  }

  // Listeners reference their handler, and handlers hold a raw listener
  // pointer for channel churn — detach before either side dies.
  void TearDown() override {
    if (handler_ != nullptr) handler_->AttachListener(nullptr);
    listener_.reset();
    GolfHubStreamFixture::TearDown();
  }

  std::shared_ptr<TicketVault> MakeVault() override {
    return std::make_shared<PgTicketVault>(std::make_shared<pg::Client>(url_),
                                           /*ticket_ttl=*/std::chrono::seconds(60),
                                           /*resume_ttl=*/std::chrono::seconds(60));
  }
  std::shared_ptr<HubStore> MakeStore() override {
    return std::make_shared<PgHubStore>(std::make_shared<pg::Client>(url_));
  }

  std::unique_ptr<pg::Listener> MakeListener(const std::shared_ptr<HubHandler>& handler) {
    auto listener = std::make_unique<pg::Listener>(
        url_, [handler](const std::string& channel, const std::string& payload) {
          handler->OnNotify(channel, payload);
        });
    handler->AttachListener(listener.get());
    return listener;
  }

  // Simulates the process dying and a fresh instance booting over the
  // same database. A crash closes nothing and says no goodbyes, so the
  // old generation is parked as-is; only the store flushes, because the
  // row truth must be complete before the successor reads it. The
  // retired hub is NOT inert: TearDown's Close() runs its clean-close
  // path and its store then writes the goodbyes a real crash never
  // would — every DB assertion must come before TearDown.
  void RestartHub() {
    handler_->AttachListener(nullptr);
    listener_.reset();
    if (store_ != nullptr) store_->Flush();
    retired_.push_back(
        {std::move(server_), std::move(client_), std::move(handler_), std::move(store_)});
    BuildHub();
    listener_ = MakeListener(handler_);
  }

  // A second live hub over the same database — the step-3 subject. Its
  // destructor unblocks parked sessions and detaches the listener before
  // the members unwind (listener before handler, by declaration order).
  struct Instance {
    std::shared_ptr<HubHandler> handler;
    std::shared_ptr<HubStore> store;
    std::unique_ptr<moonbase::golf::GolfHubServer> server;
    std::unique_ptr<moonbase::golf::GolfHubClient> client;
    std::unique_ptr<pg::Listener> listener;
    std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;
    ~Instance() {
      for (auto& session : sessions) session->Close();
      if (handler != nullptr) handler->AttachListener(nullptr);
    }
  };

  std::unique_ptr<Instance> BuildInstance() {
    auto instance = std::make_unique<Instance>();
    instance->store = MakeStore();
    instance->handler = std::make_shared<HubHandler>(
        MakeVault(), std::make_shared<cards::NoShuffleDealer>(),
        std::make_shared<RemoteIdGenerator>(),
        /*grace_period=*/std::chrono::seconds(60),
        std::make_shared<futility::otel::MetricsRecorder>("golf_hub_test"), instance->store);
    const absl::Status restored = instance->handler->RestoreFromStore();
    EXPECT_TRUE(restored.ok()) << restored;
    instance->listener = MakeListener(instance->handler);
    instance->server = std::make_unique<moonbase::golf::GolfHubServer>(instance->handler);

    auto loopback = std::make_shared<smithy::http::Loopback>();
    EXPECT_TRUE(loopback->Start(instance->server->Handler()).ok());
    smithy::ClientConfig config;
    config.retry.max_attempts = 1;
    config.http_client = loopback;
    Instance* raw = instance.get();
    config.websocket_dialer = [raw](const smithy::http::WebSocketDialRequest& request)
        -> smithy::Outcome<std::shared_ptr<smithy::http::WebSocket>> {
      auto [near, far] = smithy::http::InMemoryWebSocketPair::Create();
      smithy::http::HttpRequest upgrade;
      upgrade.method = "GET";
      upgrade.target = request.target;
      upgrade.headers = request.headers;
      raw->sessions.push_back(far);
      raw->server->StreamRouter()->ServeSession()(upgrade, far);
      return near;
    };
    auto client = moonbase::golf::GolfHubClient::Create(std::move(config));
    EXPECT_TRUE(client.ok());
    if (!client.ok()) return nullptr;
    instance->client = std::make_unique<moonbase::golf::GolfHubClient>(std::move(*client));
    return instance;
  }

  // LISTEN lands asynchronously. Foreign pokes on the room's channel
  // force refreshes until one round-trips to the seat — after this, no
  // commit's wake can slip past the seat's instance.
  bool SyncListen(Seat& seat, const std::string& room_id) {
    pg::Client db(url_);
    for (int i = 0; i < 10; ++i) {
      if (!db.Exec("SELECT pg_notify($1, 'listen-sync')", {RoomChannel(room_id)}).ok()) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return ReceiveCase(seat.stream, "roomState").has_value();
  }

  bool WaitForListenerCount(const std::string& room_id, int count) {
    pg::Client db(url_);
    const std::string statement = "LISTEN \"" + RoomChannel(room_id) + "\"";
    for (int i = 0; i < 50; ++i) {
      auto result = db.Exec(
          "SELECT count(*) FROM pg_stat_activity"
          " WHERE datname = current_database() AND query = $1",
          {statement});
      if (result.ok() && result->Get(0, 0).has_value() &&
          std::atoi(result->Get(0, 0)->c_str()) >= count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  }

  // alice on the primary instance, bob on `remote`, one room and one
  // started game between them. Every cross-instance step waits on the
  // events that prove the other instance caught up, so the flow is
  // deterministic.
  struct CrossTable {
    Seat alice;
    Seat bob;
    std::string room_id;
    std::string game_id;
  };
  std::optional<CrossTable> SeatedCrossTable(Instance& remote) {
    auto alice = OpenSeat();
    if (!alice.has_value()) return std::nullopt;
    if (!ReceiveCase(alice->stream, "sessionReady").has_value()) return std::nullopt;
    auto bob = OpenSeatVia(*remote.client);
    if (!bob.has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "sessionReady").has_value()) return std::nullopt;

    if (!alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok()) {
      return std::nullopt;
    }
    auto created = ReceiveCase(alice->stream, "roomState");
    if (!created.has_value()) return std::nullopt;
    const std::string room_id = created->as_roomState_or_null()->roomId;
    // The room's row must land before the other instance can look it
    // up, and the primary's LISTEN must be live before bob's join rider
    // fires — otherwise alice never learns of bob.
    store_->Flush();
    if (!SyncListen(*alice, room_id)) return std::nullopt;

    moonbase::golf::JoinRoom join_room;
    join_room.roomId = room_id;
    if (!bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok()) return std::nullopt;
    // bob's join materializes the room on his instance: his snapshot
    // already shows both members.
    if (!AwaitRoomState(bob->stream, [](const moonbase::golf::RoomState& room) {
           return room.players.size() == 2;
         }).has_value()) {
      return std::nullopt;
    }
    if (!SyncListen(*bob, room_id)) return std::nullopt;
    // The join's wake rider is how alice's instance learns of bob.
    if (!AwaitRoomState(alice->stream, [](const moonbase::golf::RoomState& room) {
           return room.players.size() == 2;
         }).has_value()) {
      return std::nullopt;
    }

    if (!alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    auto game_joined = ReceiveGolf(alice->stream, "gameJoined");
    if (!game_joined.has_value()) return std::nullopt;
    const std::string game_id = game_joined->as_gameJoined_or_null()->view.gameId;
    // The create commit's wake carries the lobby game to bob.
    if (!AwaitRoomState(bob->stream, [&](const moonbase::golf::RoomState& room) {
           for (const auto& game : room.games) {
             if (game.gameId == game_id) return true;
           }
           return false;
         }).has_value()) {
      return std::nullopt;
    }

    moonbase::golf::JoinGame join_game;
    join_game.gameId = game_id;
    if (!bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok()) return std::nullopt;
    if (!ReceiveGolf(bob->stream, "gameJoined").has_value()) return std::nullopt;
    // alice's instance must adopt the two-seat roster before she starts.
    if (!AwaitGameView(alice->stream, [](const moonbase::golf::GameView& view) {
           return view.players.size() == 2;
         }).has_value()) {
      return std::nullopt;
    }

    if (!alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveGolf(alice->stream, "gameStarted").has_value()) return std::nullopt;
    // bob's start signal is the projected deal (remote refresh sends
    // views, not the started event — the view is the contract).
    if (!AwaitGameView(bob->stream, [](const moonbase::golf::GameView& view) {
           return view.phase != "waiting";
         }).has_value()) {
      return std::nullopt;
    }
    return CrossTable{std::move(*alice), std::move(*bob), room_id, game_id};
  }

  // A store-side view of the rows, flushed first so staged writes are in.
  HubStore::Snapshot Rows() {
    store_->Flush();
    auto snapshot = store_->LoadSnapshot();
    EXPECT_TRUE(snapshot.ok()) << snapshot.status();
    return snapshot.value_or(HubStore::Snapshot{});
  }

  const char* url_ = nullptr;
  std::unique_ptr<pg::Listener> listener_;

  struct Generation {
    std::unique_ptr<moonbase::golf::GolfHubServer> server;
    std::unique_ptr<moonbase::golf::GolfHubClient> client;
    std::shared_ptr<HubHandler> handler;
    std::shared_ptr<HubStore> store;
  };
  std::vector<Generation> retired_;
};

namespace {

TEST_F(PgGolfHubFixture, LiveGameSurvivesARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Opening reveal for both, one hide, then alice draws and discards —
  // deck order and the discard pile now matter.
  for (auto* seat : {&alice, &bob}) {
    for (const int index : {0, 3}) {
      moonbase::golf::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  // Drain both seats to the same sync point (undelivered frames keep the
  // registry's delivery chain holding the stream through teardown),
  // remembering bob's last full view before the turn handoff — the
  // post-discard fanout, which the restored view must match.
  std::optional<moonbase::golf::GameView> before;
  for (int i = 0; i < 16; ++i) {
    auto update = ReceiveGolf(bob.stream, "gameState");
    if (!update.has_value()) break;
    before.emplace(update->as_gameState_or_null()->view);
    if (before->discardCount == 2) break;  // the post-discard fanout
  }
  ASSERT_TRUE(before.has_value());
  ASSERT_EQ(before->discardCount, 2);
  auto turn = ReceiveGolf(bob.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(alice.stream, "turnChanged").has_value());

  const int64_t version_before = [&] {
    auto rows = Rows();
    EXPECT_EQ(rows.games.size(), 1u);
    return rows.games.empty() ? 0 : rows.games[0].version;
  }();
  ASSERT_GT(version_before, 0);

  // The deploy: this process's hub dies, a fresh one boots from the
  // database. Resume tokens are rows (step 1), so the same identities
  // walk back in.
  const std::string alice_token = alice.resume_token;
  const std::string bob_token = bob.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  EXPECT_EQ(alice_back->player_id, alice.player_id);
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*ready->as_sessionReady_or_null()->roomId, table->room_id);
  ASSERT_TRUE(ReceiveGolf(alice_back->stream, "gameJoined").has_value());

  auto bob_back = OpenSeat(bob_token);
  ASSERT_TRUE(bob_back.has_value());
  EXPECT_EQ(bob_back->player_id, bob.player_id);
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());
  auto resynced = ReceiveGolf(bob_back->stream, "gameJoined");
  ASSERT_TRUE(resynced.has_value());

  // Not a diorama, and not a re-deal either: bob's restored view matches
  // the one he had — same piles, same discard top, same revealed cards.
  const auto& after = resynced->as_gameJoined_or_null()->view;
  EXPECT_EQ(after.gameId, table->game_id);
  EXPECT_EQ(after.phase, "playing");
  EXPECT_EQ(after.currentPlayerId, bob.player_id);
  EXPECT_EQ(after.drawPileCount, before->drawPileCount);
  EXPECT_EQ(after.discardCount, before->discardCount);
  ASSERT_TRUE(after.discardTop.has_value());
  ASSERT_TRUE(before->discardTop.has_value());
  EXPECT_EQ(after.discardTop->rank, before->discardTop->rank);
  EXPECT_EQ(after.discardTop->suit, before->discardTop->suit);
  for (std::size_t seat = 0; seat < after.players.size(); ++seat) {
    EXPECT_EQ(after.players[seat].revealedIndexes, before->players[seat].revealedIndexes);
    for (std::size_t slot = 0; slot < 4; ++slot) {
      const auto& mine = after.players[seat].cards[slot].card;
      const auto& theirs = before->players[seat].cards[slot].card;
      ASSERT_EQ(mine.has_value(), theirs.has_value());
      if (mine.has_value()) {
        EXPECT_EQ(mine->rank, theirs->rank);
        EXPECT_EQ(mine->suit, theirs->suit);
      }
    }
  }

  // The restored game is live: bob's turn carries on, the move fans out
  // to the restored alice, and the save continues the version sequence —
  // a reset-to-1 restore would be laundered by the repair path and never
  // fail an assertion elsewhere.
  ASSERT_TRUE(bob_back->stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob_back->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto next_turn = ReceiveGolf(alice_back->stream, "turnChanged");
  ASSERT_TRUE(next_turn.has_value());
  EXPECT_EQ(next_turn->as_turnChanged_or_null()->playerId, alice.player_id);
  ASSERT_TRUE(ReceiveGolf(bob_back->stream, "turnChanged").has_value());

  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].version, version_before + 2);  // draw + discard
}

TEST_F(PgGolfHubFixture, StatsSurviveARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Quickest legal game: alice knocks unseen, bob takes his final turn.
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "playerKnocked").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{})))
          .ok());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameEnded").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameEnded").has_value());

  const std::string alice_token = table->alice.resume_token;
  RestartHub();

  // Restart ignores terminal rows in memory but must not delete the
  // durable handoff another live instance may still need.
  {
    auto rows = Rows();
    ASSERT_EQ(rows.games.size(), 1u);
    ASSERT_TRUE(rows.games[0].state.has_value());
    EXPECT_TRUE(rows.games[0].state->isOver());
  }

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  ASSERT_TRUE(ReceiveCase(alice_back->stream, "sessionReady").has_value());
  auto restored = alice_back->stream.Receive();
  ASSERT_TRUE(restored.ok());
  ASSERT_TRUE(restored->has_value());
  ASSERT_EQ((*restored)->case_name(), "roomState");
  const auto* restored_lobby = (*restored)->as_roomState_or_null();
  ASSERT_NE(restored_lobby, nullptr);
  EXPECT_TRUE(restored_lobby->games.empty());

  ASSERT_TRUE(
      alice_back->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  // After the automatic resume snapshot, the next frame is the requested
  // lobby itself: no restored game resync or ceremony was queued.
  auto lobby = alice_back->stream.Receive();
  ASSERT_TRUE(lobby.ok());
  ASSERT_TRUE(lobby->has_value());
  ASSERT_EQ((*lobby)->case_name(), "roomState");
  const auto* lobby_state = (*lobby)->as_roomState_or_null();
  ASSERT_NE(lobby_state, nullptr);
  EXPECT_TRUE(lobby_state->games.empty());
  // Identical zero-scoring deals; the knocker takes the tie alone.
  for (const auto& player : lobby_state->players) {
    EXPECT_EQ(player.gamesPlayed, 1);
    EXPECT_EQ(player.totalScore, 0);
    EXPECT_EQ(player.gamesWon, player.playerId == alice_back->player_id ? 1 : 0);
  }
}

TEST_F(PgGolfHubFixture, PendingGameLifecycleWritesThrough) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  // A second, pending game beside the started one: bob leaves his seat in
  // the started game first, creates a fresh lobby game, and its roster
  // rides the row. (Leaving the started two-seat game ends it.)
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto created = ReceiveGolf(table->bob.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string pending_id = created->as_gameJoined_or_null()->view.gameId;

  {
    auto rows = Rows();
    ASSERT_EQ(rows.games.size(), 2u);
    for (const auto& row : rows.games) {
      if (row.game_id == pending_id) {
        EXPECT_FALSE(row.state.has_value());
        EXPECT_EQ(row.roster, (std::vector<std::string>{table->bob.player_id}));
      } else {
        EXPECT_EQ(row.game_id, table->game_id);
        ASSERT_TRUE(row.state.has_value());
        EXPECT_TRUE(row.state->isOver());
      }
    }
  }

  // Abandoning the pending game deletes only its row; the completed
  // game's terminal handoff remains with the room.
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].game_id, table->game_id);
  ASSERT_TRUE(rows.games[0].state.has_value());
  EXPECT_TRUE(rows.games[0].state->isOver());
  EXPECT_EQ(rows.rooms.size(), 1u);
  EXPECT_EQ(rows.members.size(), 2u);
}

TEST_F(PgGolfHubFixture, CorruptRowsLoseTheGameNotTheLobby) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  store_->Flush();

  // A row whose state bytes stopped decoding (schema bump gone wrong,
  // torn write, hand edit): the boot drops the game, keeps the lobby.
  {
    pg::Client db(url_);
    ASSERT_TRUE(db.Exec("UPDATE games SET state = '{\"v\":99}'::jsonb").ok());
  }
  const std::string alice_token = table->alice.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  ASSERT_TRUE(
      alice_back->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto lobby = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  EXPECT_TRUE(lobby->as_roomState_or_null()->games.empty());
  // Not wedged: the seat can start over.
  ASSERT_TRUE(
      alice_back->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice_back->stream, "gameJoined").has_value());
}

TEST_F(PgGolfHubFixture, ConnectedFlagFollowsPresence) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  {
    auto rows = Rows();
    ASSERT_EQ(rows.members.size(), 1u);
    EXPECT_TRUE(rows.members[0].connected);
  }

  // Across a restart the row keeps its last written value; the member
  // restores disconnected in memory and flips the row back on resume.
  const std::string token = alice->resume_token;
  RestartHub();
  auto alice_back = OpenSeat(token);
  ASSERT_TRUE(alice_back.has_value());
  ASSERT_TRUE(ReceiveCase(alice_back->stream, "sessionReady").has_value());
  auto rows = Rows();
  ASSERT_EQ(rows.members.size(), 1u);
  EXPECT_TRUE(rows.members[0].connected);
}

// The step-3 headline (#1194): two live hubs, one game. alice plays on
// the primary instance, bob on the second; every move is a conditional
// commit whose NOTIFY wakes the other side into re-reading and
// re-projecting. No instance ever talks to the other directly.
TEST_F(PgGolfHubFixture, TwoInstancesShareOneGame) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto table = SeatedCrossTable(*remote);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  const int64_t version_at_start = [&] {
    auto rows = Rows();
    EXPECT_EQ(rows.games.size(), 1u);
    return rows.games.empty() ? 0 : rows.games[0].version;
  }();
  ASSERT_GT(version_at_start, 0);

  // Opening reveals from both sides of the wire; both projections must
  // converge on everyone having peeked.
  for (auto* seat : {&alice, &bob}) {
    for (const int index : {0, 3}) {
      moonbase::golf::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(AwaitGameView(alice.stream, [](const moonbase::golf::GameView& view) {
                return view.allPlayersPeeked;
              }).has_value());
  ASSERT_TRUE(AwaitGameView(bob.stream, [](const moonbase::golf::GameView& view) {
                return view.allPlayersPeeked;
              }).has_value());

  // alice's turn on her instance: hide, draw, discard...
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  // ...lands on bob's instance as two discards and the turn handoff.
  ASSERT_TRUE(AwaitGameView(bob.stream, [&](const moonbase::golf::GameView& view) {
                return view.discardCount == 2 && view.currentPlayerId == bob.player_id;
              }).has_value());

  // bob answers from his instance, and alice's projection follows.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  ASSERT_TRUE(AwaitGameView(alice.stream, [&](const moonbase::golf::GameView& view) {
                return view.discardCount == 3 && view.currentPlayerId == alice.player_id;
              }).has_value());

  // Every move was exactly one landed commit: 4 peeks + hide + 2 draws
  // + 2 discards continue the version sequence without a gap — a fork,
  // a double-commit, or a lost move would all break the arithmetic.
  remote->store->Flush();
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].version, version_at_start + 9);
}

// A game finished on one instance must end exactly once everywhere: the
// finisher runs its local ceremony off the atomic finish commit; the
// other instance's wake finds the ended row and runs its own. The stat
// deltas ride the finish statement, so a replay cannot double-count.
TEST_F(PgGolfHubFixture, RemoteFinishRunsOneCeremonyEverywhere) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto table = SeatedCrossTable(*remote);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Quickest legal game: alice knocks unseen on her instance...
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok());
  // ...and the knocked phase reaches bob's projection.
  ASSERT_TRUE(AwaitGameView(bob.stream, [&](const moonbase::golf::GameView& view) {
                return view.knockedPlayerId == alice.player_id;
              }).has_value());
  // Hold Alice's listener behind the finish and the finishing instance's
  // writer drain. This makes the lost-handoff race deterministic.
  handler_->AttachListener(nullptr);
  listener_.reset();
  // bob's final turn finishes the game on the other instance.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());

  auto bob_ended = ReceiveGolf(bob.stream, "gameEnded");
  ASSERT_TRUE(bob_ended.has_value());
  // The terminal row is the durable ceremony handoff. Even if the
  // finishing instance drains its writer before the other listener
  // handles the wake, the other instance must still be able to read it.
  remote->store->Flush();
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  ASSERT_TRUE(rows.games[0].state.has_value());
  EXPECT_TRUE(rows.games[0].state->isOver());

  // Reattach after the terminal write is fully settled. Wait until both
  // instances have issued LISTEN before poking the primary.
  listener_ = MakeListener(handler_);
  ASSERT_TRUE(WaitForListenerCount(table->room_id, 2));
  pg::Client db(url_);
  ASSERT_TRUE(db.Exec("SELECT pg_notify($1, 'finish-sync')", {RoomChannel(table->room_id)}).ok());
  auto alice_ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(alice_ended.has_value());
  // Identical zero-scoring deals; the knocker takes the tie alone.
  EXPECT_EQ(alice_ended->as_gameEnded_or_null()->winner, alice.player_id);

  // Stats applied exactly once. The terminal row remains as a tombstone
  // until the room is deleted, when the foreign-key cascade removes it.
  rows = Rows();
  EXPECT_EQ(rows.games.size(), 1u);
  ASSERT_EQ(rows.members.size(), 2u);
  for (const auto& member : rows.members) {
    EXPECT_EQ(member.games_played, 1);
    EXPECT_EQ(member.total_score, 0);
    EXPECT_EQ(member.games_won, member.player_id == alice.player_id ? 1 : 0);
  }
}

TEST_F(PgGolfHubFixture, EmptiedRoomVanishesFromTheDatabase) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  auto rows = Rows();
  EXPECT_TRUE(rows.rooms.empty());
  EXPECT_TRUE(rows.members.empty());
  EXPECT_TRUE(rows.games.empty());
}

}  // namespace
}  // namespace golf_hub
