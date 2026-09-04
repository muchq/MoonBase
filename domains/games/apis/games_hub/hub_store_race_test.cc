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
#include "smithy/client/config.h"
#include "smithy/core/outcome.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

namespace games_hub {
namespace {

using moonbase::games::GolfCommands;
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
    return delegate_.LoadRoom(room_id);
  }

  bool WaitForFinish(std::chrono::milliseconds timeout) { return finish_landed_.WaitFor(timeout); }
  void AllowFinishReturn() { allow_finish_return_.Open(); }
  int delete_game_count() const { return delete_game_count_.load(); }

 private:
  MemoryHubStore delegate_;
  TestGate finish_landed_;
  TestGate allow_finish_return_;
  std::atomic<int> delete_game_count_ = 0;
};

class HubStoreRaceFixture : public GamesHubStreamFixture {
 protected:
  struct Instance {
    std::shared_ptr<GolfHub> golf;
    std::shared_ptr<CapturingMetricsRecorder> metrics;
    std::unique_ptr<moonbase::games::GamesHubServer> server;
    std::unique_ptr<moonbase::games::GamesHubClient> client;
    std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;

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
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());

  // No OnNotify: the active signal alone must carry bob onto alice's
  // projection the way a missed wake would after (re)LISTEN.
  golf_->OnChannelActive(RoomChannel(room_id));
  auto refreshed = ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
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
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  // Remote never materialized this room; a phantom channel is not a room.
  remote->golf->OnChannelActive(RoomChannel(room_id));
  remote->golf->OnChannelActive(RoomChannel("never-existed"));

  // Primary stays healthy and can still host a join the ordinary way.
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());
}

TEST_F(HubStoreRaceFixture, DelayedFinishWakeReadsRetainedTerminalRow) {
  auto remote = BuildInstance();
  ASSERT_NE(remote, nullptr);
  auto alice = OpenSeat();
  auto bob = OpenSeatVia(*remote->client);
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(alice->stream, "sessionReady"); }, remote.get())
                  .has_value());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "sessionReady"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
      [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get());
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(bob->stream, "roomState"); }, remote.get())
                  .has_value());
  golf_->OnNotify(RoomChannel(room_id), "remote-join");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
                  [&] { return ReceiveCase(alice->stream, "roomState"); }, remote.get())
                  .has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveWithin<std::optional<moonbase::games::GolfUpdate>>(
      [&] { return ReceiveGolf(alice->stream, "gameJoined"); }, remote.get());
  ASSERT_TRUE(joined.has_value());
  const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
  remote->golf->OnNotify(RoomChannel(room_id), "primary-create");
  ASSERT_TRUE(ReceiveWithin<std::optional<moonbase::games::GolfEvents>>(
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

}  // namespace
}  // namespace games_hub
