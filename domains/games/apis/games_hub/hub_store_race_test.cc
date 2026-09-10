#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"
#include "domains/games/libs/cards/dealer.h"
#include "opal/client/config.h"
#include "opal/core/outcome.h"
#include "opal/http/loopback.h"
#include "opal/http/message.h"
#include "opal/http/websocket.h"
#include "opal/http/websocket_pair.h"

namespace games_hub {
namespace {

using moonbase::games::GameCommands;
using moonbase::games::GolfMove;

class TestGate {
 public:
  void Open() {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      open_ = true;
    }
    cv_.notify_all();
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return open_; });
  }

  bool WaitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, timeout, [this] { return open_; });
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool open_ = false;
};

class GatedHubStore final : public HubStore {
 public:
  void Enqueue(std::vector<Op> ops) override {
    for (const Op& op : ops) {
      if (std::holds_alternative<DeleteGame>(op)) ++delete_game_count_;
    }
    delegate_.Enqueue(std::move(ops));
  }

  void Flush() override { delegate_.Flush(); }
  absl::StatusOr<Snapshot> LoadSnapshot() override { return delegate_.LoadSnapshot(); }
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override {
    return delegate_.CommitGameSave(row, notify_payload);
  }
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& stats,
                                        const std::string& notify_payload) override {
    auto landed = delegate_.CommitGameFinish(row, stats, notify_payload);
    if (landed.ok() && *landed) {
      finish_landed_.Open();
      allow_finish_return_.Wait();
    }
    return landed;
  }
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override {
    return delegate_.LoadGame(room_id, game_id);
  }
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override {
    auto rows = delegate_.LoadRoom(room_id);
    // The rows are already in hand: parking here is what a catch-up
    // looks like when the hub moves between its read and its reconcile.
    std::unique_lock<std::mutex> lock(load_mu_);
    if (holds_left_ == 0) return rows;
    --holds_left_;
    ++parked_;
    load_cv_.notify_all();
    load_cv_.wait(lock, [this] { return releases_ > 0; });
    --releases_;
    return rows;
  }

  bool WaitForFinish(std::chrono::milliseconds timeout) { return finish_landed_.WaitFor(timeout); }
  void AllowFinishReturn() { allow_finish_return_.Open(); }
  // The next `count` room reads park after reading.
  void HoldLoadRooms(int count) {
    const std::lock_guard<std::mutex> lock(load_mu_);
    holds_left_ = count;
  }
  // Whether a read is parked; consumes one parked read.
  bool WaitForParkedLoadRoom(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(load_mu_);
    if (!load_cv_.wait_for(lock, timeout, [this] { return parked_ > 0; })) return false;
    --parked_;
    return true;
  }
  // Lets `count` parked (or future) reads return.
  void ReleaseLoadRooms(int count) {
    {
      const std::lock_guard<std::mutex> lock(load_mu_);
      releases_ += count;
    }
    load_cv_.notify_all();
  }
  int delete_game_count() const { return delete_game_count_.load(); }

 private:
  MemoryHubStore delegate_;
  TestGate finish_landed_;
  TestGate allow_finish_return_;
  std::mutex load_mu_;
  std::condition_variable load_cv_;
  int holds_left_ = 0;
  int parked_ = 0;
  int releases_ = 0;
  std::atomic<int> delete_game_count_ = 0;
};

class HubStoreRaceFixture : public GamesHubStreamFixture {
 protected:
  struct Instance {
    std::shared_ptr<GolfHub> golf;
    std::shared_ptr<CapturingMetricsRecorder> metrics;
    std::unique_ptr<moonbase::games::GamesHubServer> server;
    std::unique_ptr<moonbase::games::GamesHubClient> client;
    std::vector<std::shared_ptr<opal::http::WebSocket>> sessions;

    // The emit→declare sweep for this instance's own recorder — the fixture's
    // TearDown only covers the primary's (#1327).
    ~Instance() {
      for (auto& session : sessions) session->Close();
      ExpectOnlyDeclaredCounterSeries(*metrics);
    }
  };

  std::shared_ptr<HubStore> MakeStore() override { return gated_store_; }

  std::unique_ptr<Instance> BuildInstance() {
    auto instance = std::make_unique<Instance>();
    instance->metrics = MakeCapturingMetricsRecorder();
    auto vault =
        std::make_shared<InMemoryTicketVault>(std::chrono::seconds(60), std::chrono::seconds(60));
    auto ids = std::make_shared<RemoteIdGenerator>();
    instance->golf = std::make_shared<GolfHub>(
        vault, std::make_shared<cards::NoShuffleDealer>(), ids, std::chrono::seconds(60),
        instance->metrics, gated_store_, /*chat_store=*/nullptr, UnlimitedRateLimits());
    EXPECT_TRUE(instance->golf->RestoreFromStore().ok());
    instance->server = std::make_unique<moonbase::games::GamesHubServer>(
        std::make_shared<GamesHubHandler>(vault, ids, instance->golf));

    auto loopback = std::make_shared<opal::http::Loopback>();
    EXPECT_TRUE(loopback->Start(instance->server->Handler()).ok());
    opal::ClientConfig config;
    config.retry.max_attempts = 1;
    config.http_client = loopback;
    Instance* raw = instance.get();
    config.websocket_dialer = [raw](const opal::http::WebSocketDialRequest& request)
        -> opal::Outcome<std::shared_ptr<opal::http::WebSocket>> {
      auto [near, far] = opal::http::InMemoryWebSocketPair::Create();
      opal::http::HttpRequest upgrade;
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

  void CloseAllSessions(Instance* remote) {
    for (auto& session : sessions_) session->Close();
    if (remote != nullptr) {
      for (auto& session : remote->sessions) session->Close();
    }
  }

  template <typename Result, typename Receive>
  Result ReceiveWithin(Receive&& receive, Instance* remote) {
    auto result = std::async(std::launch::async, std::forward<Receive>(receive));
    if (result.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
      CloseAllSessions(remote);
      ADD_FAILURE() << "stream receive exceeded one second";
      result.wait();
      return Result{};
    }
    return result.get();
  }

  std::shared_ptr<GatedHubStore> gated_store_ = std::make_shared<GatedHubStore>();

  // A catch-up on its own thread whose next `holds` room reads park with
  // their rows in hand. Going out of scope releases every read and joins,
  // so an ASSERT that returns early neither destroys a joinable thread
  // (std::terminate, and the rest of the binary with it) nor leaves the
  // store parked; declare it after the seats it touches.
  class HeldCatchUp {
   public:
    HeldCatchUp(HubStoreRaceFixture& fixture, const std::string& room_id, int holds = 1)
        : store_(fixture.gated_store_), holds_(holds) {
      store_->HoldLoadRooms(holds);
      GolfHub* golf = fixture.golf_.get();
      thread_ = std::thread([golf, room_id] { golf->OnChannelActive(RoomChannel(room_id)); });
    }
    ~HeldCatchUp() { Join(); }
    // Whether a read has its rows and is parked.
    bool WaitForRead() { return store_->WaitForParkedLoadRoom(std::chrono::seconds(1)); }
    // Lets the parked read return; the catch-up goes on to its next.
    void ReleaseRead() {
      store_->ReleaseLoadRooms(1);
      --holds_;
    }
    void Join() {
      if (!thread_.joinable()) return;
      store_->HoldLoadRooms(0);
      store_->ReleaseLoadRooms(holds_);
      thread_.join();
    }

   private:
    std::shared_ptr<GatedHubStore> store_;
    int holds_;
    std::thread thread_;
  };
};

// Room catch-up on channel-active (#1276): rows committed while an
// instance was not subscribed queued no notification; the active signal
// must re-read and re-project like a wake — same contract as chat's
// ActiveSignalHealsAMissedNotify.
TEST_F(HubStoreRaceFixture, ActiveSignalRefreshesHeldRoom) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());

  // No OnNotify: the active signal alone must carry bob onto alice's
  // projection the way a missed wake would after (re)LISTEN.
  golf_->OnChannelActive(RoomChannel(room_id));
  auto refreshed = ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(refreshed.has_value());
  EXPECT_EQ(refreshed->as_roomState_or_null()->players.size(), 2u);
}

// Membership guard on room active: never materialize from a catch-up.
TEST_F(HubStoreRaceFixture, ActiveSignalForUnheldRoomIsIgnored) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  // Remote never materialized this room; a phantom channel is not a room.
  remote->golf->OnChannelActive(RoomChannel(room_id));
  remote->golf->OnChannelActive(RoomChannel("never-existed"));

  // Primary stays healthy and can still host a join the ordinary way.
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());
}

TEST_F(HubStoreRaceFixture, DelayedFinishWakeReadsRetainedTerminalRow) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());
  golf_->OnNotify(RoomChannel(room_id), "remote-join");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
      [&] { return ReceiveGolf(alice->stream, "gameJoined"); }, remote.get());
  ASSERT_TRUE(joined.has_value());
  const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
  remote->golf->OnNotify(RoomChannel(room_id), "primary-create");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GameEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());

  moonbase::games::JoinGame join_game;
  join_game.gameId = game_id;
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
                  [&] { return ReceiveGolf(bob->stream, "gameJoined"); }, remote.get())
                  .has_value());
  golf_->OnNotify(RoomChannel(room_id), "remote-seat");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
                  [&] { return ReceiveGolf(alice->stream, "gameState"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::games::StartGame{}))).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
                  [&] { return ReceiveGolf(alice->stream, "gameStarted"); }, remote.get())
                  .has_value());
  remote->golf->OnNotify(RoomChannel(room_id), "primary-start");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
                  [&] { return ReceiveGolf(bob->stream, "gameState"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  remote->golf->OnNotify(RoomChannel(room_id), "primary-knock");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
                  [&] { return ReceiveGolf(bob->stream, "gameState"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());

  bool discard_sent = false;
  TestGate finisher_done;
  std::thread finisher([&] {
    discard_sent =
        bob->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok();
    finisher_done.Open();
  });
  const bool finish_landed = gated_store_->WaitForFinish(std::chrono::seconds(1));
  EXPECT_TRUE(finish_landed);
  gated_store_->AllowFinishReturn();
  const bool finish_completed = finisher_done.WaitFor(std::chrono::seconds(1));
  if (!finish_completed) CloseAllSessions(remote.get());
  EXPECT_TRUE(finish_completed);
  finisher.join();
  if (!finish_completed) return;
  ASSERT_TRUE(discard_sent);
  ASSERT_EQ(gated_store_->delete_game_count(), 0);

  auto terminal = gated_store_->LoadGame(room_id, game_id);
  ASSERT_TRUE(terminal.ok());
  ASSERT_TRUE(terminal->has_value());
  ASSERT_TRUE((*terminal)->state.has_value());
  EXPECT_TRUE(IsOver(*(*terminal)->state));

  golf_->OnNotify(RoomChannel(room_id), "delayed-finish");
  auto alice_ended = ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
      [&] { return ReceiveGolf(alice->stream, "gameEnded"); }, remote.get());
  EXPECT_TRUE(alice_ended.has_value());
}

// A LISTEN-active catch-up reads the room off mu_, and the hub keeps
// moving while it does: here bob's leave ends the started game and his
// next create opens a lobby game, both after the read and before the
// reconcile. Rows from before those writes must not roll the room back
// to that moment — the ended game returning as live, bob's seat moved
// onto it, the lobby game dropped locally with its row left behind
// (#1431).
TEST_F(HubStoreRaceFixture, StaleCatchUpReadDoesNotRollBackLocalWrites) {
  // bob's leave ends the two-seat game; a parked finish gate would hold
  // that leave forever.
  gated_store_->AllowFinishReturn();
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  HeldCatchUp catch_up(*this, table->room_id);
  ASSERT_TRUE(catch_up.WaitForRead());

  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto created = ReceiveGolf(table->bob.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string pending_id = created->as_gameJoined_or_null()->view.gameId;

  catch_up.Join();

  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  auto left = ReceiveGolf(table->bob.stream, "gameLeft");
  ASSERT_TRUE(left.has_value());
  EXPECT_EQ(left->as_gameLeft_or_null()->gameId, pending_id);
  gated_store_->Flush();
  auto rows = gated_store_->LoadSnapshot();
  ASSERT_TRUE(rows.ok());
  ASSERT_EQ(rows->games.size(), 1u);
  EXPECT_EQ(rows->games[0].game_id, table->game_id);
}

// The same read, straddling queued writes instead of a synchronous
// commit, and a room that keeps moving: every retried read lands beside
// another write, so the catch-up ends up reading under the lock. Stale
// rows must not seat bob back or bring alice's abandoned game back — and
// an active catch-up that found nothing moved stays silent, the locked
// read included.
TEST_F(HubStoreRaceFixture, StaleCatchUpReadsRetryThenLockAndStaySilent) {
  auto room = SeatedRoom(2);
  ASSERT_TRUE(room.has_value());
  Seat& alice = room->seats[0];
  Seat& bob = room->seats[1];
  ASSERT_TRUE(ReceiveCase(alice.stream, "roomState").has_value());  // bob's join

  HeldCatchUp catch_up(*this, room->room_id, GolfHub::kCatchUpReadAttempts);
  ASSERT_TRUE(catch_up.WaitForRead());
  ASSERT_TRUE(bob.stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(bob.stream, "roomLeft").has_value());
  auto shrunk = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(shrunk.has_value());
  ASSERT_EQ(shrunk->as_roomState_or_null()->players.size(), 1u);

  catch_up.ReleaseRead();
  ASSERT_TRUE(catch_up.WaitForRead());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameJoined").has_value());
  ASSERT_TRUE(ReceiveCase(alice.stream, "roomState").has_value());

  catch_up.ReleaseRead();
  ASSERT_TRUE(catch_up.WaitForRead());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameLeft").has_value());
  ASSERT_TRUE(ReceiveCase(alice.stream, "roomState").has_value());

  catch_up.Join();
  ExpectNoEvent(alice.stream);

  ASSERT_TRUE(
      alice.stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto after = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after->as_roomState_or_null()->players[0].playerId, alice.player_id);
  EXPECT_TRUE(after->as_roomState_or_null()->games.empty());
}

// The room can also move locally with no write at all: a join for a code
// this instance does not hold refreshes the room under the lock and
// adopts a sibling's change. A catch-up whose read predates that
// adoption must not undo it. Here the sibling drops bob, a refused join
// adopts the drop, and the stale rows would seat him back.
TEST_F(HubStoreRaceFixture, StaleCatchUpReadDoesNotUndoAnAdoption) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GameCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GameCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  golf_->OnNotify(RoomChannel(room_id), "remote-join");
  auto both = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(both.has_value());
  ASSERT_EQ(both->as_roomState_or_null()->players.size(), 2u);

  HeldCatchUp catch_up(*this, room_id);
  ASSERT_TRUE(catch_up.WaitForRead());

  ASSERT_TRUE(bob->stream.Send(GameCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomLeft").has_value());
  moonbase::games::JoinGame join_game;
  join_game.gameId = "NOSUCH";
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok());
  auto adopted = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(adopted.has_value());
  ASSERT_EQ(adopted->as_roomState_or_null()->players.size(), 1u);
  ASSERT_TRUE(ReceiveCase(alice->stream, "commandRejected").has_value());

  catch_up.Join();

  ASSERT_TRUE(
      alice->stream.Send(GameCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto after = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

}  // namespace
}  // namespace games_hub
