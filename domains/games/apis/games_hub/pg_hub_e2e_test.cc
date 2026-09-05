// The step-2/3 persistence e2e (#1194): the same client flows as
// hub_e2e_test, but over durable credentials (step 1) and the rooms/games
// write-through — then the process "dies" (RestartHub) and a fresh hub
// over the same database has to seat everyone back into their live game.
// The step-3 suite adds a second live instance (BuildInstance): one room
// and one game shared across two hubs whose only channel is the database
// and its NOTIFY wire. Real postgres via GAMES_HUB_TEST_DB_URL; skips
// otherwise.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "domains/games/apis/games_hub/migrations.h"
#include "domains/games/apis/games_hub/pg_chat_store.h"
#include "domains/games/apis/games_hub/pg_hub_store.h"
#include "domains/games/apis/games_hub/pg_ticket_vault.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/platform/libs/pg/listener.h"
#include "domains/platform/libs/pg/pg.h"

namespace games_hub {

using moonbase::games::GameCommands;
using moonbase::games::GolfMove;

class PgGamesHubFixture : public GamesHubStreamFixture {
 protected:
  void SetUp() override {
    url_ = std::getenv("GAMES_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GAMES_HUB_TEST_DB_URL unset";
    }
    pg::Client db(url_);
    ASSERT_TRUE(RunMigrations(db).ok());
    ASSERT_TRUE(db.Exec("TRUNCATE rooms CASCADE").ok());
    ASSERT_TRUE(db.Exec("TRUNCATE tickets, resume_tokens").ok());
    GamesHubStreamFixture::SetUp();
    listener_ = MakeListener(golf_);
  }

  // Listeners reference their handler, and handlers hold a raw listener
  // pointer for channel churn — detach before either side dies.
  void TearDown() override {
    if (golf_ != nullptr) golf_->AttachListener(nullptr);
    listener_.reset();
    GamesHubStreamFixture::TearDown();
  }

  std::shared_ptr<TicketVault> MakeVault() override {
    return std::make_shared<PgTicketVault>(std::make_shared<pg::Client>(url_),
                                           /*ticket_ttl=*/std::chrono::seconds(60),
                                           /*resume_ttl=*/std::chrono::seconds(60));
  }
  std::shared_ptr<HubStore> MakeStore() override {
    return std::make_shared<PgHubStore>(std::make_shared<pg::Client>(url_));
  }
  std::shared_ptr<ChatStore> MakeChatStore() override {
    return std::make_shared<PgChatStore>(std::make_shared<pg::Client>(url_));
  }

  std::unique_ptr<pg::Listener> MakeListener(const std::shared_ptr<GolfHub>& golf) {
    auto listener = std::make_unique<pg::Listener>(
        url_,
        [golf](const std::string& channel, const std::string& payload) {
          golf->OnNotify(channel, payload);
        },
        // The active signal is the catch-up trigger for chat and rooms:
        // rows committed before a (re)LISTEN landed never notified this
        // instance (#1276).
        [golf](const std::string& channel) { golf->OnChannelActive(channel); });
    golf->AttachListener(listener.get());
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
    golf_->AttachListener(nullptr);
    listener_.reset();
    if (store_ != nullptr) store_->Flush();
    retired_.push_back(
        {std::move(server_), std::move(client_), std::move(golf_), std::move(store_)});
    BuildHub();
    listener_ = MakeListener(golf_);
  }

  // A second live hub over the same database — the step-3 subject. Its
  // destructor unblocks parked sessions and detaches the listener before
  // the members unwind (listener before golf, by declaration order).
  struct Instance {
    std::shared_ptr<GolfHub> golf;
    std::shared_ptr<CapturingMetricsRecorder> metrics;
    std::shared_ptr<HubStore> store;
    std::unique_ptr<moonbase::games::GamesHubServer> server;
    std::unique_ptr<moonbase::games::GamesHubClient> client;
    std::unique_ptr<pg::Listener> listener;
    std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;
    // The emit→declare sweep for this instance's own recorder — the fixture's
    // TearDown only covers the primary's (#1327).
    ~Instance() {
      for (auto& session : sessions) session->Close();
      if (golf != nullptr) golf->AttachListener(nullptr);
      ExpectOnlyDeclaredCounterSeries(*metrics);
    }
  };

  std::unique_ptr<Instance> BuildInstance() {
    auto instance = std::make_unique<Instance>();
    instance->store = MakeStore();
    instance->metrics = MakeCapturingMetricsRecorder();
    auto vault = MakeVault();
    auto ids = std::make_shared<RemoteIdGenerator>();
    instance->golf =
        std::make_shared<GolfHub>(vault, std::make_shared<cards::NoShuffleDealer>(), ids,
                                  /*grace_period=*/std::chrono::seconds(60), instance->metrics,
                                  instance->store, MakeChatStore(), UnlimitedRateLimits());
    const absl::Status restored = instance->golf->RestoreFromStore();
    EXPECT_TRUE(restored.ok()) << restored;
    instance->listener = MakeListener(instance->golf);
    instance->server =
        std::make_unique<moonbase::games::GamesHubServer>(std::make_shared<GamesHubHandler>(
            vault, ids, instance->golf, std::make_shared<ThoughtsHub>(vault, instance->metrics)));

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
    auto client = moonbase::games::GamesHubClient::Create(std::move(config));
    EXPECT_TRUE(client.ok());
    if (!client.ok()) return nullptr;
    instance->client = std::make_unique<moonbase::games::GamesHubClient>(std::move(*client));
    return instance;
  }

  // LISTEN lands asynchronously. Foreign pokes on the room's channel
  // force refreshes until one round-trips to the seat — after this, no
  // commit's wake can slip past the seat's instance.
  bool SyncListen(Seat& seat, const std::string& room_id) {
    pg::Client db(url_);
    for (int i = 0; i < 10; ++i) {
      if (!db.Exec("SELECT pg_notify($1, 'listen-sync')", {RoomChannel(room_id)}).ok()) {
        ADD_FAILURE() << "LISTEN sync poke failed for " << RoomChannel(room_id);
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (ReceiveCase(seat.stream, "roomState").has_value()) return true;
    ADD_FAILURE() << "LISTEN sync for " << RoomChannel(room_id)
                  << " never produced roomState on seat " << seat.player_id;
    return false;
  }

  bool WaitForListenerCount(const std::string& room_id, int count) {
    pg::Client db(url_);
    // pg_stat_activity.query is a backend's *last* statement. The poll
    // thread LISTENs a room's two channels in sorted order (chat before
    // room), but a wake between AttachListener's two Listen() calls can
    // split them across passes and leave the chat LISTEN as the one that
    // sticks. Either statement therefore counts: both are only ever
    // wanted together, so a backend showing one has the other active or
    // lands it within the same pass.
    const std::string room_statement = "LISTEN \"" + RoomChannel(room_id) + "\"";
    const std::string chat_statement = "LISTEN \"" + ChatChannel(room_id) + "\"";
    for (int i = 0; i < 50; ++i) {
      auto result = db.Exec(
          "SELECT count(*) FROM pg_stat_activity"
          " WHERE datname = current_database() AND query IN ($1, $2)",
          {room_statement, chat_statement});
      if (result.ok() && result->Get(0, 0).has_value() &&
          std::atoi(result->Get(0, 0)->c_str()) >= count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
  }

  // Pulls already-queued frames off a seat so the registry's async
  // delivery chain can finish. smithy-cpp#173 (send-before-receive on
  // terminal transitions) fixed the End()/Close deadlock; draining here
  // still keeps TearDown deterministic when wake frames are unread.
  static void DrainPending(moonbase::games::PlayClientStream& stream) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < deadline) {
      auto received = stream.Receive(std::chrono::milliseconds(5));
      if (!received.ok() || !received->has_value()) return;
    }
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

  void DetachListeners(Instance& remote) {
    if (golf_ != nullptr) golf_->AttachListener(nullptr);
    listener_.reset();
    if (remote.golf != nullptr) remote.golf->AttachListener(nullptr);
    remote.listener.reset();
  }

  // Stop both instances' listeners, then drain unread wake frames. Call
  // before CrossTable/Instance go out of scope — a late OnChannelActive
  // after DrainPending would refill the chain and TearDown hangs again.
  void QuiesceCrossTable(Instance& remote, CrossTable& table) {
    DetachListeners(remote);
    DrainPending(table.alice.stream);
    DrainPending(table.bob.stream);
  }

  void QuiesceSeats(Instance& remote, Seat& alice, Seat& bob) {
    DetachListeners(remote);
    DrainPending(alice.stream);
    DrainPending(bob.stream);
  }

  // Holds an optional<CrossTable>* so it can be armed *before*
  // SeatedCrossTable returns — a lost wake during setup must still
  // detach listeners, or named ADD_FAILURE text dies with a 60s SIGKILL.
  struct QuiesceOnScopeExit {
    PgGamesHubFixture* fixture = nullptr;
    Instance* remote = nullptr;
    std::optional<CrossTable>* table = nullptr;
    Seat* alice = nullptr;
    Seat* bob = nullptr;
    ~QuiesceOnScopeExit() {
      if (fixture == nullptr || remote == nullptr) return;
      if (table != nullptr && table->has_value()) {
        fixture->QuiesceCrossTable(*remote, **table);
      } else if (alice != nullptr && bob != nullptr) {
        fixture->QuiesceSeats(*remote, *alice, *bob);
      } else {
        fixture->DetachListeners(*remote);
      }
    }
  };

  // Seats opened by cross-table setup, drained if it fails mid-way —
  // destroying the client stream while the server still has unread wake
  // frames is the TearDown hang, and QuiesceOnScopeExit cannot see them.
  struct CrossSeats {
    std::optional<Seat> alice;
    std::optional<Seat> bob;
    ~CrossSeats() {
      if (alice.has_value()) DrainPending(alice->stream);
      if (bob.has_value()) DrainPending(bob->stream);
    }
  };

  // alice on the primary, bob on `remote`, both in one room whose every
  // cross-instance step has landed. Empty on failure.
  std::string SeatedCrossRoom(Instance& remote, CrossSeats& seats) {
    seats.alice = OpenSeat();
    if (!seats.alice.has_value()) return "";
    if (!ReceiveCase(seats.alice->stream, "sessionReady").has_value()) return "";
    seats.bob = OpenSeatVia(*remote.client);
    if (!seats.bob.has_value()) return "";
    if (!ReceiveCase(seats.bob->stream, "sessionReady").has_value()) return "";

    if (!seats.alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{}))
             .ok()) {
      return "";
    }
    auto created = ReceiveCase(seats.alice->stream, "roomState");
    if (!created.has_value()) return "";
    const std::string room_id = created->as_roomState_or_null()->roomId;
    // The room's row must land before the other instance can look it
    // up, and the primary's LISTEN must be live before bob's join rider
    // fires — otherwise alice never learns of bob.
    store_->Flush();
    if (!SyncListen(*seats.alice, room_id)) return "";

    moonbase::games::JoinRoom join_room;
    join_room.roomId = room_id;
    if (!seats.bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok()) return "";
    // bob's join materializes the room on his instance: his snapshot
    // already shows both members.
    if (!AwaitRoomState(
             seats.bob->stream,
             [](const moonbase::games::RoomState& room) { return room.players.size() == 2; },
             "bob (remote) roomState with 2 players after join")
             .has_value()) {
      return "";
    }
    if (!SyncListen(*seats.bob, room_id)) return "";
    // The join's wake rider is how alice's instance learns of bob.
    if (!AwaitRoomState(
             seats.alice->stream,
             [](const moonbase::games::RoomState& room) { return room.players.size() == 2; },
             "alice (primary) roomState with 2 players after bob's join wake")
             .has_value()) {
      return "";
    }
    return room_id;
  }

  // The create commit's wake carries the lobby game to bob.
  bool AwaitLobbyGame(Seat& bob, const std::string& game_id) {
    return AwaitRoomState(
               bob.stream,
               [&](const moonbase::games::RoomState& room) {
                 for (const auto& game : room.games) {
                   if (game.gameId == game_id) return true;
                 }
                 return false;
               },
               "bob (remote) roomState carrying alice's created game " + game_id)
        .has_value();
  }

  std::optional<CrossTable> SeatedCrossTable(Instance& remote) {
    CrossSeats seats;
    const std::string room_id = SeatedCrossRoom(remote, seats);
    if (room_id.empty()) return std::nullopt;

    if (!seats.alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto game_joined = ReceiveGolf(seats.alice->stream, "gameJoined");
    if (!game_joined.has_value()) return std::nullopt;
    const std::string game_id = game_joined->as_gameJoined_or_null()->view.gameId;
    if (!AwaitLobbyGame(*seats.bob, game_id)) return std::nullopt;

    moonbase::games::JoinGame join_game;
    join_game.gameId = game_id;
    if (!seats.bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok()) return std::nullopt;
    if (!ReceiveGolf(seats.bob->stream, "gameJoined").has_value()) return std::nullopt;
    // alice's instance must adopt the two-seat roster before she starts.
    if (!AwaitGameView(
             seats.alice->stream,
             [](const moonbase::games::GameView& view) { return view.players.size() == 2; },
             "alice (primary) gameView with 2 players after bob's seat wake")
             .has_value()) {
      return std::nullopt;
    }

    if (!seats.alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::games::StartGame{})))
             .ok()) {
      return std::nullopt;
    }
    if (!ReceiveGolf(seats.alice->stream, "gameStarted").has_value()) return std::nullopt;
    // bob's start signal is the projected deal (remote refresh sends
    // views, not the started event — the view is the contract).
    if (!AwaitGameView(
             seats.bob->stream,
             [](const moonbase::games::GameView& view) { return view.phase != "waiting"; },
             "bob (remote) gameView dealt after alice's start wake")
             .has_value()) {
      return std::nullopt;
    }
    CrossTable table{std::move(*seats.alice), std::move(*seats.bob), room_id, game_id};
    seats.alice.reset();
    seats.bob.reset();
    return table;
  }

  // The same two seats at a castle table, dealt and in setup.
  std::optional<CrossTable> SeatedCrossCastleTable(Instance& remote) {
    using moonbase::games::CastleMove;
    CrossSeats seats;
    const std::string room_id = SeatedCrossRoom(remote, seats);
    if (room_id.empty()) return std::nullopt;

    if (!seats.alice->stream.Send(Castle(CastleMove::FromCreategame(moonbase::games::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto game_joined = ReceiveCastle(seats.alice->stream, "gameJoined");
    if (!game_joined.has_value()) return std::nullopt;
    const std::string game_id = game_joined->as_gameJoined_or_null()->view.gameId;
    if (!AwaitLobbyGame(*seats.bob, game_id)) return std::nullopt;

    moonbase::games::JoinGame join_game;
    join_game.gameId = game_id;
    if (!seats.bob->stream.Send(Castle(CastleMove::FromJoingame(join_game))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveCastle(seats.bob->stream, "gameJoined").has_value()) return std::nullopt;
    if (!AwaitCastleView(
             seats.alice->stream,
             [](const moonbase::games::CastleView& view) { return view.players.size() == 2; },
             "alice (primary) castle view with 2 players after bob's seat wake")
             .has_value()) {
      return std::nullopt;
    }

    if (!seats.alice->stream.Send(Castle(CastleMove::FromStartgame(moonbase::games::StartGame{})))
             .ok()) {
      return std::nullopt;
    }
    if (!ReceiveCastle(seats.alice->stream, "gameStarted").has_value()) return std::nullopt;
    if (!AwaitCastleView(
             seats.bob->stream,
             [](const moonbase::games::CastleView& view) { return view.phase == "setup"; },
             "bob (remote) castle view dealt after alice's start wake")
             .has_value()) {
      return std::nullopt;
    }
    CrossTable table{std::move(*seats.alice), std::move(*seats.bob), room_id, game_id};
    seats.alice.reset();
    seats.bob.reset();
    return table;
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
    std::unique_ptr<moonbase::games::GamesHubServer> server;
    std::unique_ptr<moonbase::games::GamesHubClient> client;
    std::shared_ptr<GolfHub> golf;
    std::shared_ptr<HubStore> store;
  };
  std::vector<Generation> retired_;
};

namespace {

TEST_F(PgGamesHubFixture, LiveGameSurvivesARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Opening reveal for both, one hide, then alice draws and discards —
  // deck order and the discard pile now matter.
  for (auto* seat : {&alice, &bob}) {
    for (const int index : {0, 3}) {
      moonbase::games::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::games::HideCards{}))).ok());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  // Drain both seats to the same sync point (undelivered frames keep the
  // registry's delivery chain holding the stream through teardown),
  // remembering bob's last full view before the turn handoff — the
  // post-discard fanout, which the restored view must match.
  std::optional<moonbase::games::GameView> before;
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
  ASSERT_TRUE(
      bob_back->stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob_back->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{})))
          .ok());
  auto next_turn = ReceiveGolf(alice_back->stream, "turnChanged");
  ASSERT_TRUE(next_turn.has_value());
  EXPECT_EQ(next_turn->as_turnChanged_or_null()->playerId, alice.player_id);
  ASSERT_TRUE(ReceiveGolf(bob_back->stream, "turnChanged").has_value());

  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].version, version_before + 2);  // draw + discard

  // The restored lobby puts them at the restored table: read off the
  // roster the row carries, not off anything the old process had.
  ASSERT_TRUE(
      alice_back->stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
          .ok());
  auto lobby = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  for (const auto& player : lobby->as_roomState_or_null()->players) {
    ASSERT_TRUE(player.table.has_value()) << player.playerId;
    EXPECT_EQ(player.table->game, "golf");
    EXPECT_EQ(player.table->gameId, table->game_id);
  }
}

TEST_F(PgGamesHubFixture, StatsSurviveARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Quickest legal game: alice knocks unseen, bob takes his final turn.
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "playerKnocked").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{})))
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
    EXPECT_TRUE(IsOver(*rows.games[0].state));
  }

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  ASSERT_TRUE(ReceiveCase(alice_back->stream, "sessionReady").has_value());
  auto restored = NextEvent(alice_back->stream);
  ASSERT_TRUE(restored.has_value());
  ASSERT_EQ(std::string(restored->case_name()), "roomState");
  const auto* restored_lobby = restored->as_roomState_or_null();
  ASSERT_NE(restored_lobby, nullptr);
  EXPECT_TRUE(restored_lobby->games.empty());

  // A resume also replays the room's chat (#1226) — one history event
  // after the snapshot, empty here since nobody said anything.
  auto replay = NextEvent(alice_back->stream);
  ASSERT_TRUE(replay.has_value());
  ASSERT_EQ(std::string(replay->case_name()), "roomChatHistory");
  EXPECT_TRUE(replay->as_roomChatHistory_or_null()->messages.empty());

  ASSERT_TRUE(
      alice_back->stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
          .ok());
  // After the resume snapshot and its chat replay, the next frame is the
  // requested lobby itself: no restored game resync or ceremony queued.
  auto lobby = NextEvent(alice_back->stream);
  ASSERT_TRUE(lobby.has_value());
  ASSERT_EQ(std::string(lobby->case_name()), "roomState");
  const auto* lobby_state = lobby->as_roomState_or_null();
  ASSERT_NE(lobby_state, nullptr);
  EXPECT_TRUE(lobby_state->games.empty());
  // Identical zero-scoring deals; the knocker takes the tie alone.
  for (const auto& player : lobby_state->players) {
    EXPECT_EQ(player.gamesPlayed, 1);
    EXPECT_EQ(player.totalScore, 0);
    EXPECT_EQ(player.gamesWon, player.playerId == alice_back->player_id ? 1 : 0);
  }
}

// A castle table is the second engine behind the same rows (#77): its
// state round-trips through the store on a restart, with the last play,
// the redaction per viewer, and the turn intact.
TEST_F(PgGamesHubFixture, CastleTableSurvivesARestart) {
  using moonbase::games::CastleMove;
  auto table = SeatedCastleTable();
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;
  ASSERT_TRUE(ReceiveCastle(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveCastle(bob.stream, "gameState").has_value());

  // Both ready as dealt; bob opens on his jack and plays it.
  for (Seat* seat : {&alice, &bob}) {
    ASSERT_TRUE(seat->stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  }
  for (Seat* seat : {&alice, &bob}) {
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  }
  moonbase::games::PlayFromHand jack;
  jack.indexes = {0};
  ASSERT_TRUE(bob.stream.Send(Castle(CastleMove::FromPlayfromhand(jack))).ok());
  auto before_update = ReceiveCastle(alice.stream, "gameState");
  ASSERT_TRUE(before_update.has_value());
  const moonbase::games::CastleView before = before_update->as_gameState_or_null()->view;
  ASSERT_EQ(before.pileCount, 1);
  ASSERT_TRUE(before.lastPlay.has_value());
  EXPECT_TRUE(before.players[0].canPlay);
  for (Seat* seat : {&alice, &bob}) {
    auto turn = ReceiveCastle(seat->stream, "turnChanged");
    ASSERT_TRUE(turn.has_value());
    EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, alice.player_id);
  }

  const int64_t version_before = [&] {
    auto rows = Rows();
    EXPECT_EQ(rows.games.size(), 1u);
    if (!rows.games.empty()) EXPECT_EQ(rows.games[0].kind, GameKind::kCastle);
    return rows.games.empty() ? 0 : rows.games[0].version;
  }();
  ASSERT_GT(version_before, 0);

  const std::string alice_token = alice.resume_token;
  const std::string bob_token = bob.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  EXPECT_EQ(alice_back->player_id, alice.player_id);
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  EXPECT_EQ(ready->as_sessionReady_or_null()->roomId.value_or(""), table->room_id);
  auto resynced = ReceiveCastle(alice_back->stream, "gameJoined");
  ASSERT_TRUE(resynced.has_value());

  // Not a re-deal: alice's restored view is the one she had, jack on the
  // pile and all, still her turn, still hers alone to play.
  const auto& after = resynced->as_gameJoined_or_null()->view;
  EXPECT_EQ(after.gameId, table->game_id);
  EXPECT_EQ(after.phase, "playing");
  EXPECT_EQ(after.currentPlayerId.value_or(""), alice.player_id);
  EXPECT_EQ(after.drawPileCount, before.drawPileCount);
  EXPECT_EQ(after.pileCount, before.pileCount);
  ASSERT_EQ(after.run.size(), 1u);
  EXPECT_EQ(after.run[0].rank + after.run[0].suit, "J♣");
  ASSERT_TRUE(after.lastPlay.has_value());
  EXPECT_EQ(after.lastPlay->playerId, bob.player_id);
  EXPECT_EQ(after.lastPlay->cards.size(), 1u);
  EXPECT_FALSE(after.lastPlay->burned);
  ASSERT_EQ(after.players.size(), 2u);
  for (std::size_t seat = 0; seat < after.players.size(); ++seat) {
    EXPECT_EQ(after.players[seat].playerId, before.players[seat].playerId);
    EXPECT_EQ(after.players[seat].handCount, before.players[seat].handCount);
    EXPECT_EQ(after.players[seat].faceDownCount, before.players[seat].faceDownCount);
    ASSERT_EQ(after.players[seat].hand.size(), before.players[seat].hand.size());
    for (std::size_t i = 0; i < after.players[seat].hand.size(); ++i) {
      EXPECT_EQ(after.players[seat].hand[i].rank, before.players[seat].hand[i].rank);
      EXPECT_EQ(after.players[seat].hand[i].suit, before.players[seat].hand[i].suit);
    }
    ASSERT_EQ(after.players[seat].faceUp.size(), before.players[seat].faceUp.size());
    for (std::size_t i = 0; i < after.players[seat].faceUp.size(); ++i) {
      EXPECT_EQ(after.players[seat].faceUp[i].rank, before.players[seat].faceUp[i].rank);
      EXPECT_EQ(after.players[seat].faceUp[i].suit, before.players[seat].faceUp[i].suit);
    }
    EXPECT_EQ(after.players[seat].canPlay, before.players[seat].canPlay);
  }
  EXPECT_EQ(after.players[0].hand.size(), 3u);
  EXPECT_TRUE(after.players[1].hand.empty());

  auto bob_back = OpenSeat(bob_token);
  ASSERT_TRUE(bob_back.has_value());
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());
  auto bob_view = ReceiveCastle(bob_back->stream, "gameJoined");
  ASSERT_TRUE(bob_view.has_value());
  EXPECT_TRUE(bob_view->as_gameJoined_or_null()->view.players[0].hand.empty());
  // His own hand, faces and all: the jack went out and a draw came in.
  EXPECT_EQ(static_cast<int>(bob_view->as_gameJoined_or_null()->view.players[1].hand.size()),
            before.players[1].handCount);
  EXPECT_FALSE(bob_view->as_gameJoined_or_null()->view.players[1].canPlay);

  // The restored table is live: alice's queen beats the jack, the move
  // fans out to the restored bob, and the save continues the version
  // sequence.
  moonbase::games::PlayFromHand queen;
  queen.indexes = {2};
  ASSERT_TRUE(alice_back->stream.Send(Castle(CastleMove::FromPlayfromhand(queen))).ok());
  auto next_turn = ReceiveCastle(bob_back->stream, "turnChanged");
  ASSERT_TRUE(next_turn.has_value());
  EXPECT_EQ(next_turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveCastle(alice_back->stream, "turnChanged").has_value());

  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].version, version_before + 1);
}

// A castle table shared across two instances: readies and plays from
// either side land on the other as projected views, one commit each.
TEST_F(PgGamesHubFixture, TwoInstancesShareOneCastleTable) {
  using moonbase::games::CastleMove;
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  std::optional<CrossTable> table;
  QuiesceOnScopeExit quiesce{this, remote.get(), &table};
  table = SeatedCrossCastleTable(*remote);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  const int64_t version_at_start = [&] {
    auto rows = Rows();
    EXPECT_EQ(rows.games.size(), 1u);
    return rows.games.empty() ? 0 : rows.games[0].version;
  }();
  ASSERT_GT(version_at_start, 0);

  // alice readies on her instance; bob's projection shows it.
  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  ASSERT_TRUE(AwaitCastleView(
                  bob.stream,
                  [](const moonbase::games::CastleView& view) { return view.players[0].ready; },
                  "bob (remote) castle view with alice ready")
                  .has_value());
  // bob readies on his; the table opens on alice's instance with bob on
  // turn and nothing for her to play yet.
  ASSERT_TRUE(bob.stream.Send(Castle(CastleMove::FromReady(moonbase::games::Ready{}))).ok());
  auto opened = AwaitCastleView(
      alice.stream,
      [&](const moonbase::games::CastleView& view) {
        return view.phase == "playing" && view.currentPlayerId == bob.player_id;
      },
      "alice (primary) castle view playing with bob on turn");
  ASSERT_TRUE(opened.has_value());
  EXPECT_FALSE(opened->players[0].canPlay);

  // bob's jack from his instance lands on alice's as the last play and
  // her turn; her queen answers and lands on his.
  moonbase::games::PlayFromHand jack;
  jack.indexes = {0};
  ASSERT_TRUE(bob.stream.Send(Castle(CastleMove::FromPlayfromhand(jack))).ok());
  auto her_turn = AwaitCastleView(
      alice.stream,
      [&](const moonbase::games::CastleView& view) {
        return view.pileCount == 1 && view.currentPlayerId == alice.player_id;
      },
      "alice (primary) turn handoff after bob's play wake");
  ASSERT_TRUE(her_turn.has_value());
  ASSERT_TRUE(her_turn->lastPlay.has_value());
  EXPECT_EQ(her_turn->lastPlay->playerId, bob.player_id);
  EXPECT_TRUE(her_turn->players[0].canPlay);
  moonbase::games::PlayFromHand queen;
  queen.indexes = {2};
  ASSERT_TRUE(alice.stream.Send(Castle(CastleMove::FromPlayfromhand(queen))).ok());
  auto his_turn = AwaitCastleView(
      bob.stream,
      [&](const moonbase::games::CastleView& view) {
        return view.pileCount == 2 && view.currentPlayerId == bob.player_id;
      },
      "bob (remote) turn handoff after alice's play wake");
  ASSERT_TRUE(his_turn.has_value());
  EXPECT_EQ(his_turn->lastPlay->playerId, alice.player_id);

  // Two readies and two plays: four landed commits, no gap.
  remote->store->Flush();
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].kind, GameKind::kCastle);
  EXPECT_EQ(rows.games[0].version, version_at_start + 4);
}

TEST_F(PgGamesHubFixture, PendingGameLifecycleWritesThrough) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  // A second, pending game beside the started one: bob leaves his seat in
  // the started game first, creates a fresh lobby game, and its roster
  // rides the row. (Leaving the started two-seat game ends it.)
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
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
        EXPECT_TRUE(IsOver(*row.state));
      }
    }
  }

  // Abandoning the pending game deletes only its row; the completed
  // game's terminal handoff remains with the room.
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].game_id, table->game_id);
  ASSERT_TRUE(rows.games[0].state.has_value());
  EXPECT_TRUE(IsOver(*rows.games[0].state));
  EXPECT_EQ(rows.rooms.size(), 1u);
  EXPECT_EQ(rows.members.size(), 2u);
}

TEST_F(PgGamesHubFixture, CorruptRowsLoseTheGameNotTheLobby) {
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
      alice_back->stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
          .ok());
  auto lobby = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  EXPECT_TRUE(lobby->as_roomState_or_null()->games.empty());
  // Not wedged: the seat can start over.
  ASSERT_TRUE(
      alice_back->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice_back->stream, "gameJoined").has_value());
}

TEST_F(PgGamesHubFixture, ConnectedFlagFollowsPresence) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  {
    auto rows = Rows();
    ASSERT_EQ(rows.members.size(), 1u);
    EXPECT_TRUE(rows.members[0].connected);
  }

  // Across a restart the row keeps its last written value, the restore
  // adopts it as presence truth, and the resume re-affirms it.
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
TEST_F(PgGamesHubFixture, TwoInstancesShareOneGame) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  std::optional<CrossTable> table;
  QuiesceOnScopeExit quiesce{this, remote.get(), &table};
  table = SeatedCrossTable(*remote);
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
      moonbase::games::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(AwaitGameView(
                  alice.stream,
                  [](const moonbase::games::GameView& view) { return view.allPlayersPeeked; },
                  "alice (primary) allPlayersPeeked after cross-instance peeks")
                  .has_value());
  ASSERT_TRUE(AwaitGameView(
                  bob.stream,
                  [](const moonbase::games::GameView& view) { return view.allPlayersPeeked; },
                  "bob (remote) allPlayersPeeked after cross-instance peeks")
                  .has_value());

  // alice's turn on her instance: hide, draw, discard...
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::games::HideCards{}))).ok());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  // ...lands on bob's instance as two discards and the turn handoff.
  ASSERT_TRUE(AwaitGameView(
                  bob.stream,
                  [&](const moonbase::games::GameView& view) {
                    return view.discardCount == 2 && view.currentPlayerId == bob.player_id;
                  },
                  "bob (remote) turn handoff after alice's discard wake")
                  .has_value());

  // bob answers from his instance, and alice's projection follows.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  ASSERT_TRUE(AwaitGameView(
                  alice.stream,
                  [&](const moonbase::games::GameView& view) {
                    return view.discardCount == 3 && view.currentPlayerId == alice.player_id;
                  },
                  "alice (primary) turn handoff after bob's discard wake")
                  .has_value());

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
TEST_F(PgGamesHubFixture, RemoteFinishRunsOneCeremonyEverywhere) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  std::optional<CrossTable> table;
  QuiesceOnScopeExit quiesce{this, remote.get(), &table};
  table = SeatedCrossTable(*remote);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Quickest legal game: alice knocks unseen on her instance...
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  // ...and the knocked phase reaches bob's projection.
  ASSERT_TRUE(AwaitGameView(
                  bob.stream,
                  [&](const moonbase::games::GameView& view) {
                    return view.knockedPlayerId == alice.player_id;
                  },
                  "bob (remote) knockedPlayerId after alice's knock wake")
                  .has_value());
  // Hold Alice's listener behind the finish and the finishing instance's
  // writer drain. This makes the lost-handoff race deterministic.
  golf_->AttachListener(nullptr);
  listener_.reset();
  // bob's final turn finishes the game on the other instance.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());

  auto bob_ended = ReceiveGolf(bob.stream, "gameEnded");
  ASSERT_TRUE(bob_ended.has_value());
  // The terminal row is the durable ceremony handoff. Even if the
  // finishing instance drains its writer before the other listener
  // handles the wake, the other instance must still be able to read it.
  remote->store->Flush();
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  ASSERT_TRUE(rows.games[0].state.has_value());
  EXPECT_TRUE(IsOver(*rows.games[0].state));

  // Reattach after the terminal write is fully settled. Wait until both
  // instances have issued LISTEN before poking the primary.
  listener_ = MakeListener(golf_);
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

// A commit loop can rebase onto a remote finish: the store hands it the
// finished state at the finisher's version, the engine refuses the move,
// and the loop returns with the ended table still in the map, its
// ceremony unpaid — the lobby keeps listing it and keeps its members at
// it, and the "leave your current game first" guard locks them out of a
// new one. The reconcile owes that ceremony whatever the versions say:
// a finished entry still held is the debt, the finished row pays it.
TEST_F(PgGamesHubFixture, ARebaseOntoARemoteFinishStillGetsTheCeremony) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  std::optional<CrossTable> table;
  QuiesceOnScopeExit quiesce{this, remote.get(), &table};
  table = SeatedCrossTable(*remote);
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Hold alice's listener behind everything that follows, so her
  // instance learns of the finish only through its own commit.
  golf_->AttachListener(nullptr);
  listener_.reset();
  // bob abandons on his instance: alice is the last seat standing, so the
  // game resolves there and its row turns terminal.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameLeft").has_value());
  ASSERT_TRUE(ReceiveCase(bob.stream, "roomState").has_value());
  remote->store->Flush();
  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  ASSERT_TRUE(rows.games[0].state.has_value());
  ASSERT_TRUE(IsOver(*rows.games[0].state));

  // alice, still at her stale turn, knocks: her commit loses to the
  // finish, rebases onto the ended state, and the engine refuses.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  auto refused = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "game is over");

  // The wake she missed arrives with the reattach: the ceremony she is
  // owed, then a lobby with the table gone and her free at last.
  listener_ = MakeListener(golf_);
  ASSERT_TRUE(WaitForListenerCount(table->room_id, 2));
  pg::Client db(url_);
  ASSERT_TRUE(db.Exec("SELECT pg_notify($1, 'finish-sync')", {RoomChannel(table->room_id)}).ok());
  auto ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(ended->as_gameEnded_or_null()->winner, alice.player_id);
  auto lobby = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  EXPECT_TRUE(lobby->as_roomState_or_null()->games.empty());
  EXPECT_EQ(TableOf(*lobby->as_roomState_or_null(), alice.player_id), Idle());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameCreated").has_value());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(ReceiveCase(alice.stream, "roomState").has_value());
}

// Pins QuiesceCrossTable: an injected unread wake must be drained, or
// deleting the QuiesceOnScopeExit lines would leave this red.
TEST_F(PgGamesHubFixture, QuiesceDrainsUnreadWakeFrames) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  std::optional<CrossTable> table;
  QuiesceOnScopeExit quiesce{this, remote.get(), &table};
  table = SeatedCrossTable(*remote);
  ASSERT_TRUE(table.has_value());

  // Extra foreign wake leaves a roomState alice has not read.
  golf_->OnNotify(RoomChannel(table->room_id), "foreign-wake");
  QuiesceCrossTable(*remote, *table);
  // Disarm the scope guard — listeners are already detached.
  quiesce.fixture = nullptr;

  auto leftover = table->alice.stream.Receive(std::chrono::milliseconds(50));
  EXPECT_TRUE(!leftover.ok() || !leftover->has_value())
      << "unread wake frame survived QuiesceCrossTable";
}

// Chat across the real wire (#1226 task 5): a message committed on one
// instance reaches the other's members via its NOTIFY — or, when the
// commit raced the receiving side's LISTEN, via the channel-active
// catch-up read. Which path fired is deliberately invisible: delivery
// is bounded either way, which is the whole at-least-once claim.
TEST_F(PgGamesHubFixture, ChatCrossesInstancesBothWays) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  QuiesceOnScopeExit quiesce{this, remote.get(), /*table=*/nullptr, &*alice, &*bob};

  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  // Room and membership rows are written through asynchronously; the
  // remote's join needs the room row, and each sender's append
  // authorizes against their member row.
  store_->Flush();

  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  remote->store->Flush();

  moonbase::games::Chat chat;
  chat.text = "hello from remote";
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromChat(chat)).ok());
  auto bob_echo = ReceiveCase(bob->stream, "roomChat");
  ASSERT_TRUE(bob_echo.has_value());
  auto to_alice = ReceiveCase(alice->stream, "roomChat");
  ASSERT_TRUE(to_alice.has_value());
  EXPECT_EQ(to_alice->as_roomChat_or_null()->text, "hello from remote");
  EXPECT_EQ(to_alice->as_roomChat_or_null()->messageId, bob_echo->as_roomChat_or_null()->messageId);

  chat.text = "hello from primary";
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  auto to_bob = ReceiveCase(bob->stream, "roomChat");
  ASSERT_TRUE(to_bob.has_value());
  EXPECT_EQ(to_bob->as_roomChat_or_null()->text, "hello from primary");
  EXPECT_GT(to_bob->as_roomChat_or_null()->messageId, bob_echo->as_roomChat_or_null()->messageId);

  // A later joiner replays both messages from the database, in id order,
  // regardless of which instance stored them.
  auto charlie = OpenSeatVia(*remote->client);
  ASSERT_TRUE(charlie.has_value());
  ASSERT_TRUE(ReceiveCase(charlie->stream, "sessionReady").has_value());
  ASSERT_TRUE(charlie->stream.Send(GameCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(charlie->stream, "roomState").has_value());
  auto replay = ReceiveCase(charlie->stream, "roomChatHistory");
  ASSERT_TRUE(replay.has_value());
  const auto* history = replay->as_roomChatHistory_or_null();
  ASSERT_EQ(history->messages.size(), 2u);
  EXPECT_EQ(history->messages[0].text, "hello from remote");
  EXPECT_EQ(history->messages[1].text, "hello from primary");
}

// LISTEN/NOTIFY has no replay: a notify fired while a listener's
// connection is down reaches no one, ever. What makes chat delivery
// at-least-once anyway is the reconnect: the re-LISTEN's channel-active
// signal triggers a catch-up read of everything the cursor missed.
TEST_F(PgGamesHubFixture, ChatCommittedDuringListenerOutageArrivesAfterReconnect) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  QuiesceOnScopeExit quiesce{this, remote.get(), /*table=*/nullptr, &*alice, &*bob};

  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  store_->Flush();
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  remote->store->Flush();

  // One exchanged message proves live delivery is up before the outage.
  moonbase::games::Chat chat;
  chat.text = "before";
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());

  // Kill every listener backend; the poll threads reconnect on their own.
  pg::Client db(url_);
  ASSERT_TRUE(db.Exec("SELECT pg_terminate_backend(pid) FROM pg_stat_activity"
                      " WHERE query LIKE 'LISTEN%' AND pid <> pg_backend_pid()")
                  .ok());

  // Committed while (most likely) nobody was subscribed. The sender's
  // own echo needs no listener — the local pump runs off the append —
  // but alice's instance can only learn of the row from a wake, and if
  // its notify fired into the gap, only the catch-up read remains.
  chat.text = "during outage";
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());
  auto healed = ReceiveCase(alice->stream, "roomChat");
  ASSERT_TRUE(healed.has_value());
  EXPECT_EQ(healed->as_roomChat_or_null()->text, "during outage");
}

TEST_F(PgGamesHubFixture, EmptiedRoomVanishesFromTheDatabase) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  // A stored message, so the vanishing below has chat to take with it.
  store_->Flush();
  moonbase::games::Chat chat;
  chat.text = "soon gone";
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  auto rows = Rows();
  EXPECT_TRUE(rows.rooms.empty());
  EXPECT_TRUE(rows.members.empty());
  EXPECT_TRUE(rows.games.empty());
  // The room row's deletion cascades to its chat: nothing retains a
  // message for a room that no longer exists.
  pg::Client db(url_);
  auto chat_rows = db.Exec("SELECT count(*) FROM room_chat_messages");
  ASSERT_TRUE(chat_rows.ok());
  ASSERT_TRUE(chat_rows->Get(0, 0).has_value());
  EXPECT_EQ(*chat_rows->Get(0, 0), "0");
}

}  // namespace
}  // namespace games_hub
