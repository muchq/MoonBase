#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/games/apis/golf_hub/hub_store.h"
#include "domains/games/apis/golf_hub/stream_test_fixture.h"
#include "domains/games/libs/cards/dealer.h"
#include "smithy/client/config.h"
#include "smithy/core/outcome.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

// The chat pump's cross-instance behavior, pinned deterministically: the
// handlers share the memory stores, and the tests hand-deliver the wakes
// PostgreSQL's LISTEN/NOTIFY would carry. The pg e2e suite proves the
// same protocol over a real wire; this suite owns the orderings a real
// wire won't schedule on demand — a notify that hasn't arrived yet, a
// duplicate wake, a wake landing mid-drain.

namespace golf_hub {
namespace {

using moonbase::golf::GolfCommands;

class TestGate {
 public:
  void Open() {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      open_ = true;
    }
    cv_.notify_all();
  }

  bool WaitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, timeout, [this] { return open_; });
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return open_; });
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool open_ = false;
};

// Forwards everything; when armed, the next load (either kind) announces
// itself and blocks until released — and, when asked, fails after the
// release instead of forwarding. That parks a pump mid-drain at a chosen
// point, the only way to deterministically land an append, a second
// wake, or a transient store failure while a load is in flight.
class GatedChatStore final : public ChatStore {
 public:
  explicit GatedChatStore(std::shared_ptr<ChatStore> delegate) : delegate_(std::move(delegate)) {}

  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override {
    return delegate_->Append(room_id, player_id, text, notify_payload);
  }
  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override {
    if (Gate()) return absl::UnavailableError("gated load failure");
    return delegate_->LoadRecent(room_id, limit);
  }
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override {
    if (Gate()) return absl::UnavailableError("gated load failure");
    return delegate_->LoadAfter(room_id, after_message_id, limit);
  }
  void DropRoom(const std::string& room_id) override { delegate_->DropRoom(room_id); }

  void ArmGate() { armed_ = true; }
  void FailOnRelease() { fail_on_release_ = true; }
  bool WaitForEntry(std::chrono::milliseconds timeout) { return entered_.WaitFor(timeout); }
  void Release() { released_.Open(); }

 private:
  // Returns true when the gated call should fail instead of forwarding.
  bool Gate() {
    if (armed_.exchange(false)) {
      entered_.Open();
      released_.Wait();
      return fail_on_release_.exchange(false);
    }
    return false;
  }

  std::shared_ptr<ChatStore> delegate_;
  std::atomic<bool> armed_ = false;
  std::atomic<bool> fail_on_release_ = false;
  TestGate entered_;
  TestGate released_;
};

// Secondary instances mint from their own space so two hubs can never
// hand out the same player id.
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

class HubChatRaceFixture : public GolfHubStreamFixture {
 protected:
  struct Instance {
    std::shared_ptr<HubHandler> handler;
    std::unique_ptr<moonbase::golf::GolfHubServer> server;
    std::unique_ptr<moonbase::golf::GolfHubClient> client;
    std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;

    ~Instance() {
      for (auto& session : sessions) session->Close();
    }
  };

  // Membership lives on whichever instance seats the sender, so the
  // shared store's guard tries the primary handler first and the remote
  // second — a fixed order, so two concurrent appends can never take the
  // two handlers' locks in opposite orders.
  std::shared_ptr<ChatStore> MakeChatStore() override {
    auto shared = std::make_shared<MemoryChatStore>([this](const std::string& room_id,
                                                           const std::string& player_id,
                                                           const MemberAction& action) {
      if (handler_ != nullptr && handler_->WithMember(room_id, player_id, action)) return true;
      return remote_ != nullptr && remote_->handler->WithMember(room_id, player_id, action);
    });
    gated_ = std::make_shared<GatedChatStore>(std::move(shared));
    return gated_;
  }

  void SetUp() override {
    GolfHubStreamFixture::SetUp();
    remote_ = BuildInstance();
    ASSERT_NE(remote_, nullptr);
  }

  // A hub sharing the primary's vault (so seats resume across instances)
  // and both stores. Restores on build, like main would.
  std::unique_ptr<Instance> BuildInstance() {
    auto instance = std::make_unique<Instance>();
    instance->handler = std::make_shared<HubHandler>(
        vault_, std::make_shared<cards::NoShuffleDealer>(), std::make_shared<RemoteIdGenerator>(),
        std::chrono::seconds(60),
        std::make_shared<futility::otel::MetricsRecorder>("golf_hub_test"), store_, chat_store_,
        UnlimitedRateLimits());
    EXPECT_TRUE(instance->handler->RestoreFromStore().ok());
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

  // Alice seated on the primary owns a room; bob joins it through the
  // remote. Neither instance has been woken about the other's activity —
  // every wake after this point is one the test delivers by hand.
  struct Pair {
    Seat alice;
    Seat bob;
    std::string room_id;
  };
  std::optional<Pair> SplitRoom() {
    auto alice = OpenSeat();
    auto bob = OpenSeatVia(*remote_->client);
    if (!alice.has_value() || !bob.has_value()) return std::nullopt;
    if (!ReceiveCase(alice->stream, "sessionReady").has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "sessionReady").has_value()) return std::nullopt;
    const std::string room_id = CreateRoomFor(*alice);
    if (room_id.empty()) return std::nullopt;
    moonbase::golf::JoinRoom join;
    join.roomId = room_id;
    if (!bob->stream.Send(GolfCommands::FromJoinroom(join)).ok()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "roomState").has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "roomChatHistory").has_value()) return std::nullopt;
    return Pair{std::move(*alice), std::move(*bob), room_id};
  }

  bool SendChat(Seat& seat, const std::string& text) {
    moonbase::golf::Chat chat;
    chat.text = text;
    return seat.stream.Send(GolfCommands::FromChat(chat)).ok();
  }

  // Asserts the stream's next frame — not merely a later one — is a
  // roomChat carrying `text`. Returns its message id, 0 on failure.
  int64_t ExpectNextChat(Seat& seat, const std::string& text) {
    auto event = NextEvent(seat.stream);
    if (!event.has_value()) return 0;
    const auto* chat = event->as_roomChat_or_null();
    if (chat == nullptr) {
      ADD_FAILURE() << "expected roomChat \"" << text << "\", got " << event->case_name();
      return 0;
    }
    EXPECT_EQ(chat->text, text);
    return chat->messageId;
  }

  std::shared_ptr<GatedChatStore> gated_;
  std::unique_ptr<Instance> remote_;
};

// The ordering the pump exists for. Bob's message commits on the remote
// while its notify to the primary is still "in flight" (here: never
// delivered — the tests own the wire). Alice's append must not step over
// it: her own pump walks the cursor and delivers bob's earlier row first,
// with no wake involved. An append path that staged only its own row and
// advanced the cursor past it would skip bob's message on the primary
// for good.
TEST_F(HubChatRaceFixture, RacedRemoteCommitReachesLocalsInIdOrder) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  ASSERT_TRUE(SendChat(pair->bob, "from bob"));
  ASSERT_GT(ExpectNextChat(pair->bob, "from bob"), 0);

  ASSERT_TRUE(SendChat(pair->alice, "from alice"));
  const int64_t bobs = ExpectNextChat(pair->alice, "from bob");
  const int64_t alices = ExpectNextChat(pair->alice, "from alice");
  ASSERT_GT(bobs, 0);
  EXPECT_GT(alices, bobs);

  // The remote hears about alice's row the ordinary way: its wake. The
  // chat pump ignores the payload — even an own-instance payload pumps.
  remote_->handler->OnNotify(ChatChannel(pair->room_id), "some-instance");
  EXPECT_EQ(ExpectNextChat(pair->bob, "from alice"), alices);
}

// A single wake delivers everything committed since the cursor, however
// many pages that takes — a burst wide enough to cross three LoadAfter
// pages arrives complete and in order off one notify.
TEST_F(HubChatRaceFixture, OneWakeDrainsEverythingCommittedAcrossPages) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  const int total = static_cast<int>(kChatCatchUpPage) * 2 + 3;
  for (int i = 1; i <= total; ++i) {
    ASSERT_TRUE(SendChat(pair->alice, absl::StrCat("m", i)));
  }
  for (int i = 1; i <= total; ++i) {
    ASSERT_GT(ExpectNextChat(pair->alice, absl::StrCat("m", i)), 0);
  }

  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  int64_t last = 0;
  for (int i = 1; i <= total; ++i) {
    const int64_t id = ExpectNextChat(pair->bob, absl::StrCat("m", i));
    EXPECT_GT(id, last);
    last = id;
  }
}

// Notifies are at-least-once and our own commits wake us too; all the
// redundancy lands on the cursor and delivers nothing twice. The proof
// is ordering: after the extra wakes, the very next frame each side
// receives is the sentinel, not a replay.
TEST_F(HubChatRaceFixture, RedundantWakesDeliverNothingNew) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  ASSERT_TRUE(SendChat(pair->alice, "first"));
  ASSERT_GT(ExpectNextChat(pair->alice, "first"), 0);
  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  ASSERT_GT(ExpectNextChat(pair->bob, "first"), 0);

  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");  // duplicate
  handler_->OnNotify(ChatChannel(pair->room_id), "own-commit");       // our own echo

  ASSERT_TRUE(SendChat(pair->alice, "sentinel"));
  ASSERT_GT(ExpectNextChat(pair->alice, "sentinel"), 0);
  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  ASSERT_GT(ExpectNextChat(pair->bob, "sentinel"), 0);
}

// The listener's channel-active signal is a wake like any other: rows
// committed while an instance was not subscribed queued no notification,
// and the signal alone heals the gap.
TEST_F(HubChatRaceFixture, ActiveSignalHealsAMissedNotify) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  ASSERT_TRUE(SendChat(pair->alice, "missed"));
  ASSERT_GT(ExpectNextChat(pair->alice, "missed"), 0);

  remote_->handler->OnChannelActive(ChatChannel(pair->room_id));
  ASSERT_GT(ExpectNextChat(pair->bob, "missed"), 0);
}

// Wakes for rooms an instance does not hold — dropped rooms, rooms it
// never materialized, channels that never existed — deliver nothing and
// resurrect nothing, and leave the instance healthy.
TEST_F(HubChatRaceFixture, WakesForUnheldRoomsAreIgnored) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  // The remote never materialized this room, and the last one is not a
  // room at all.
  remote_->handler->OnNotify(ChatChannel(room_id), "wake");
  remote_->handler->OnChannelActive(ChatChannel(room_id));
  remote_->handler->OnNotify(ChatChannel("never-existed"), "wake");

  ASSERT_TRUE(SendChat(*alice, "still works"));
  ASSERT_GT(ExpectNextChat(*alice, "still works"), 0);

  // And the remote can still meet the room the ordinary way afterwards.
  auto bob = OpenSeatVia(*remote_->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  auto replay = ReceiveCase(bob->stream, "roomChatHistory");
  ASSERT_TRUE(replay.has_value());
  ASSERT_EQ(replay->as_roomChatHistory_or_null()->messages.size(), 1u);
  EXPECT_EQ(replay->as_roomChatHistory_or_null()->messages[0].text, "still works");
}

// An instance that restores a room from the store (a restart, from the
// store's point of view) seeds the room's cursor at the newest retained
// id during the restore itself, so the past is never replayed live —
// history replay owns it. The seed then stands where the restore left
// it, so a row committed between the restore and a member's resume is
// heard twice: once in the resume's replay and once live. That overlap
// is the model's documented contract — at-least-once, dedupe by id —
// and this pins it on purpose.
TEST_F(HubChatRaceFixture, RestoredInstanceSeedsTheCursorInsteadOfReplayingThePast) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  ASSERT_TRUE(SendChat(*alice, "old"));
  ASSERT_GT(ExpectNextChat(*alice, "old"), 0);
  ASSERT_GT(ExpectNextChat(*bob, "old"), 0);

  // A third instance restores the room, cursor seeded at "old". A chat
  // wake right after delivers nothing — its members are all disconnected —
  // which is the point: the past is not replayed at anyone.
  auto restored = BuildInstance();
  ASSERT_NE(restored, nullptr);
  restored->handler->OnNotify(ChatChannel(room_id), "wake");

  ASSERT_TRUE(SendChat(*alice, "mid"));
  ASSERT_GT(ExpectNextChat(*alice, "mid"), 0);
  ASSERT_GT(ExpectNextChat(*bob, "mid"), 0);

  // Bob resumes onto the restored instance; the replay hands him
  // everything retained, including what committed after adoption.
  auto resumed = OpenSeatVia(*restored->client, bob->resume_token);
  ASSERT_TRUE(resumed.has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "roomState").has_value());
  auto replay = ReceiveCase(resumed->stream, "roomChatHistory");
  ASSERT_TRUE(replay.has_value());
  const auto* history = replay->as_roomChatHistory_or_null();
  ASSERT_EQ(history->messages.size(), 2u);
  EXPECT_EQ(history->messages[0].text, "old");
  EXPECT_EQ(history->messages[1].text, "mid");

  ASSERT_TRUE(SendChat(*alice, "new"));
  ASSERT_GT(ExpectNextChat(*alice, "new"), 0);
  ASSERT_GT(ExpectNextChat(*bob, "new"), 0);

  // One wake after the resume: live delivery starts at the restore's
  // seed, so "mid" overlaps the replay and "old" — behind the cursor —
  // does not repeat.
  restored->handler->OnNotify(ChatChannel(room_id), "wake");
  const int64_t mid_id = ExpectNextChat(*resumed, "mid");
  const int64_t new_id = ExpectNextChat(*resumed, "new");
  ASSERT_GT(mid_id, 0);
  EXPECT_GT(new_id, mid_id);
}

// The regression drill for the seed-at-birth rule, in the schedule the
// pg suite once caught by chance (~1 in 10): a chat wake's store load is
// in flight on an instance at the moment a local member's append
// commits. Wake-time cursor creation used to adopt "the newest retained
// id" from that off-lock read, which classified the raced append as the
// past — consumed without delivery, so its sender never saw their own
// message. With cursors born with the room, the parked drain reads
// pages above the seed and must deliver the append when it resumes.
TEST_F(HubChatRaceFixture, AppendCommittingDuringAnInFlightWakeLoadIsStillDelivered) {
  // The room and both memberships predate the third instance; bob's
  // seat arrives there by resume, the shape the pg flake had.
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  auto restored = BuildInstance();
  ASSERT_NE(restored, nullptr);

  // Park the wake's load before bob's seat exists on this instance, so
  // the pump's view of the room predates everything that follows.
  gated_->ArmGate();
  std::thread wake([&] { restored->handler->OnChannelActive(ChatChannel(room_id)); });
  const bool entered = gated_->WaitForEntry(std::chrono::seconds(5));
  EXPECT_TRUE(entered);

  auto resumed = OpenSeatVia(*restored->client, bob->resume_token);
  ASSERT_TRUE(resumed.has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(resumed->stream, "roomChatHistory").has_value());

  // Commits while the wake's load is parked; the append's own pump
  // coalesces into the parked drain rather than starting a second one.
  ASSERT_TRUE(SendChat(*resumed, "raced"));

  gated_->Release();
  wake.join();

  // The resumed drain hands bob his own message — exactly once, which
  // the sentinel's arrival as the very next frame then proves.
  ASSERT_GT(ExpectNextChat(*resumed, "raced"), 0);
  ASSERT_TRUE(SendChat(*alice, "sentinel"));
  ASSERT_GT(ExpectNextChat(*alice, "raced"), 0);  // primary catches up off its own pump
  ASSERT_GT(ExpectNextChat(*alice, "sentinel"), 0);
  restored->handler->OnNotify(ChatChannel(room_id), "primary");
  ASSERT_GT(ExpectNextChat(*resumed, "sentinel"), 0);
}

// A transient store failure must not eat a wake that coalesced into the
// failing drain: that signal may be the only one an already-committed
// append gets (its own pump call already came and coalesced), so the
// drain spends it on one immediate retry. The gate parks the remote's
// catch-up load, lands the second wake, then fails the parked load on
// release — with no further wakes, the retry is bob's only path.
TEST_F(HubChatRaceFixture, WakeCoalescedIntoAFailingLoadStillDelivers) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  ASSERT_TRUE(SendChat(pair->alice, "storm"));
  ASSERT_GT(ExpectNextChat(pair->alice, "storm"), 0);

  gated_->ArmGate();
  gated_->FailOnRelease();
  std::thread wake([&] { remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary"); });
  const bool entered = gated_->WaitForEntry(std::chrono::seconds(5));
  EXPECT_TRUE(entered);
  if (entered) {
    remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");  // coalesces
  }
  gated_->Release();
  wake.join();

  ASSERT_GT(ExpectNextChat(pair->bob, "storm"), 0);
}

// A wake that lands while a drain is already running must neither start
// a second concurrent drain nor be dropped: the drainer notices and
// loops once more. The gate parks the first pump inside its LoadAfter so
// the second wake deterministically hits a drain in flight.
TEST_F(HubChatRaceFixture, WakeLandingMidDrainCoalescesIntoOneDelivery) {
  auto pair = SplitRoom();
  ASSERT_TRUE(pair.has_value());

  ASSERT_TRUE(SendChat(pair->alice, "before"));
  ASSERT_GT(ExpectNextChat(pair->alice, "before"), 0);
  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  ASSERT_GT(ExpectNextChat(pair->bob, "before"), 0);

  ASSERT_TRUE(SendChat(pair->alice, "gated"));
  ASSERT_GT(ExpectNextChat(pair->alice, "gated"), 0);

  gated_->ArmGate();
  std::thread first_wake(
      [&] { remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary"); });
  const bool entered = gated_->WaitForEntry(std::chrono::seconds(5));
  EXPECT_TRUE(entered);
  if (entered) {
    // Lands while the first drain is parked inside LoadAfter: returns
    // immediately, leaving the drainer one more loop to run.
    remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  }
  gated_->Release();
  first_wake.join();

  // Exactly one delivery for the coalesced pair of wakes; the sentinel
  // arriving as bob's very next frame proves no duplicate snuck out.
  ASSERT_GT(ExpectNextChat(pair->bob, "gated"), 0);
  ASSERT_TRUE(SendChat(pair->alice, "sentinel"));
  ASSERT_GT(ExpectNextChat(pair->alice, "sentinel"), 0);
  remote_->handler->OnNotify(ChatChannel(pair->room_id), "primary");
  ASSERT_GT(ExpectNextChat(pair->bob, "sentinel"), 0);
}

}  // namespace
}  // namespace golf_hub
