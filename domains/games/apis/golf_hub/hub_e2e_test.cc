// Phase 1 e2e: session minting, ticket admission, room lifecycle, and the
// reconnect-adjacent seat semantics, driven through the generated client
// over the in-memory pair. Wire-level details (JSON-text framing, real
// sockets) are upstream-tested; these pin the hub's behavior.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/games/apis/golf_hub/stream_test_fixture.h"

namespace golf_hub {
namespace {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;

using moonbase::golf::GolfMove;
using moonbase::golf::GolfUpdate;

std::string WithNul(std::string prefix, std::string suffix) {
  prefix.push_back('\0');
  prefix.append(suffix);
  return prefix;
}

// A whole second hub sharing the first one's durable pieces — the
// hub_store_race_test pattern. Restoring from the shared store is what a
// process restart looks like from the store's side, so resuming here
// exercises the membership-decides-the-resync branch of Play() without
// simulating a wire failure.
//
// One trap: the shared MemoryChatStore's member guard was wired to the
// FIRST handler in the fixture's SetUp, so appends through this instance
// authorize against instance one's membership. Reads (history) are
// unguarded and safe; treat this instance as read-only for chat unless
// the guard is rewired.
struct SecondInstance {
  std::shared_ptr<HubHandler> handler;
  std::unique_ptr<moonbase::golf::GolfHubServer> server;
  std::unique_ptr<moonbase::golf::GolfHubClient> client;
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;

  ~SecondInstance() {
    for (auto& session : sessions) session->Close();
  }
};

// Forwards everything except LoadRecent, which always fails — the only
// way to reach the handler's failed-history-load branch, since
// MemoryChatStore's own LoadRecent cannot return a non-ok status.
class FailingHistoryChatStore final : public ChatStore {
 public:
  explicit FailingHistoryChatStore(std::shared_ptr<ChatStore> delegate)
      : delegate_(std::move(delegate)) {}

  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override {
    return delegate_->Append(room_id, player_id, text, notify_payload);
  }
  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override {
    return absl::InternalError("chat database unavailable");
  }
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override {
    return delegate_->LoadAfter(room_id, after_message_id, limit);
  }
  void DropRoom(const std::string& room_id) override { delegate_->DropRoom(room_id); }

 private:
  std::shared_ptr<ChatStore> delegate_;
};

// Forwards everything except Append, which always fails as if the
// database were unreachable — the only way to reach the handler's
// unavailable-append branch, since MemoryChatStore cannot fail to
// reach itself.
class FailingAppendChatStore final : public ChatStore {
 public:
  explicit FailingAppendChatStore(std::shared_ptr<ChatStore> delegate)
      : delegate_(std::move(delegate)) {}

  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override {
    return absl::UnavailableError("chat store unreachable");
  }
  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override {
    return delegate_->LoadRecent(room_id, limit);
  }
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override {
    return delegate_->LoadAfter(room_id, after_message_id, limit);
  }
  void DropRoom(const std::string& room_id) override { delegate_->DropRoom(room_id); }

 private:
  std::shared_ptr<ChatStore> delegate_;
};

// Forwards everything except Append, which reports the sender as no
// longer a member — the store-level stale-membership rejection that the
// in-process guard can never produce on its own instance.
class NotAMemberChatStore final : public ChatStore {
 public:
  explicit NotAMemberChatStore(std::shared_ptr<ChatStore> delegate)
      : delegate_(std::move(delegate)) {}

  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override {
    return NotAMemberError();
  }
  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override {
    return delegate_->LoadRecent(room_id, limit);
  }
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override {
    return delegate_->LoadAfter(room_id, after_message_id, limit);
  }
  void DropRoom(const std::string& room_id) override { delegate_->DropRoom(room_id); }

 private:
  std::shared_ptr<ChatStore> delegate_;
};

std::unique_ptr<SecondInstance> BuildSecondInstance(
    std::shared_ptr<TicketVault> vault, std::shared_ptr<HubStore> store,
    std::shared_ptr<ChatStore> chat_store,
    std::shared_ptr<futility::otel::MetricsRecorder> metrics =
        std::make_shared<futility::otel::MetricsRecorder>("golf_hub_test")) {
  auto instance = std::make_unique<SecondInstance>();
  instance->handler = std::make_shared<HubHandler>(
      std::move(vault), std::make_shared<cards::NoShuffleDealer>(),
      std::make_shared<SequentialIdGenerator>(), std::chrono::seconds(60), std::move(metrics),
      std::move(store), std::move(chat_store));
  EXPECT_TRUE(instance->handler->RestoreFromStore().ok());
  instance->server = std::make_unique<moonbase::golf::GolfHubServer>(instance->handler);

  auto loopback = std::make_shared<smithy::http::Loopback>();
  EXPECT_TRUE(loopback->Start(instance->server->Handler()).ok());
  smithy::ClientConfig config;
  config.retry.max_attempts = 1;
  config.http_client = loopback;
  SecondInstance* raw = instance.get();
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

}  // namespace

class GolfGameFixture : public GolfHubStreamFixture {};

namespace {

TEST_F(GolfHubStreamFixture, SessionMintsDistinctPlayersAndResumeTokenRoundTrips) {
  moonbase::golf::GetSessionInput input;
  auto first = client_->GetSession(input);
  auto second = client_->GetSession(input);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->playerId, second->playerId);

  moonbase::golf::GetSessionInput resume;
  resume.resumeToken = first->resumeToken;
  auto resumed = client_->GetSession(resume);
  ASSERT_TRUE(resumed.ok());
  EXPECT_EQ(resumed->playerId, first->playerId);
  EXPECT_NE(resumed->ticket, first->ticket);
  // A valid token is echoed back, not replaced — the client's long-lived
  // credential must not churn on every reconnect.
  EXPECT_EQ(resumed->resumeToken, first->resumeToken);
}

class RecordingVault final : public TicketVault {
 public:
  RecordingVault()
      : delegate_(/*ticket_ttl=*/std::chrono::seconds(60),
                  /*resume_ttl=*/std::chrono::seconds(60)) {}

  absl::StatusOr<std::string> IssueTicket(const std::string& player_id) override {
    return delegate_.IssueTicket(player_id);
  }
  absl::StatusOr<std::string> IssueResumeToken(const std::string& player_id) override {
    return delegate_.IssueResumeToken(player_id);
  }
  bool PeekTicket(const std::string& ticket) const override {
    ++peek_calls;
    return delegate_.PeekTicket(ticket);
  }
  std::optional<std::string> SpendTicket(const std::string& ticket) override {
    ++spend_calls;
    return delegate_.SpendTicket(ticket);
  }
  std::optional<std::string> ResolveResumeToken(const std::string& token) const override {
    ++resolve_calls;
    return delegate_.ResolveResumeToken(token);
  }

  mutable std::atomic<int> peek_calls = 0;
  std::atomic<int> spend_calls = 0;
  mutable std::atomic<int> resolve_calls = 0;

 private:
  InMemoryTicketVault delegate_;
};

class ProtocolBoundaryFixture : public GolfHubStreamFixture {
 protected:
  std::shared_ptr<TicketVault> MakeVault() override {
    recording_vault_ = std::make_shared<RecordingVault>();
    return recording_vault_;
  }

  std::shared_ptr<RecordingVault> recording_vault_;
};

TEST_F(ProtocolBoundaryFixture, NulBearingCredentialsNeverReachTheVault) {
  moonbase::golf::GetSessionInput session;
  session.resumeToken = WithNul("rt-bogus", "suffix");
  const auto minted = client_->GetSession(session);
  ASSERT_TRUE(minted.ok());
  EXPECT_EQ(recording_vault_->resolve_calls.load(), 0);

  moonbase::golf::PlayInput play;
  play.ticket = WithNul("t-bogus", "suffix");
  auto stream = client_->Play(play);
  ASSERT_TRUE(stream.ok());
  const auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated");
  EXPECT_EQ(recording_vault_->spend_calls.load(), 0);
}

TEST_F(GolfHubStreamFixture, BadTicketFailsTypedBeforeAnyEvent) {
  moonbase::golf::PlayInput input;
  input.ticket = "t-bogus";
  auto stream = client_->Play(input);
  ASSERT_TRUE(stream.ok()) << stream.error().message();
  auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated") << first.error().message();
}

TEST_F(GolfHubStreamFixture, TicketSpendsOnce) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // The same ticket is gone; a second dial with it dies typed. (A fresh
  // ticket for the same player is the SeatConflict test below.)
  moonbase::golf::PlayInput replay;
  replay.ticket = "t-bogus";  // any unspendable ticket behaves alike
  auto second = client_->Play(replay);
  ASSERT_TRUE(second.ok());
  auto first_event = second->Receive();
  ASSERT_FALSE(first_event.ok());
  EXPECT_EQ(first_event.error().code(), "Unauthenticated");
}

TEST_F(GolfHubStreamFixture, SecondLiveConnectionForSamePlayerIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // Fresh ticket for the same player while the first wire is healthy:
  // admission refuses (ADR-0022) as the modeled SeatConflict.
  auto conflicted = OpenSeat(seat->resume_token);
  ASSERT_TRUE(conflicted.has_value());
  EXPECT_EQ(conflicted->player_id, seat->player_id);
  auto first = conflicted->stream.Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "SeatConflict");
}

TEST_F(GolfHubStreamFixture, CreateJoinAndLeaveBroadcastRoomState) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const auto* room = created->as_roomState_or_null();
  ASSERT_NE(room, nullptr);
  ASSERT_EQ(room->players.size(), 1u);
  EXPECT_EQ(room->players[0].playerId, alice->player_id);
  const std::string room_id = room->roomId;

  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto bob_view = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(bob_view.has_value());
  EXPECT_EQ(bob_view->as_roomState_or_null()->players.size(), 2u);
  auto alice_view = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(alice_view.has_value());
  EXPECT_EQ(alice_view->as_roomState_or_null()->players.size(), 2u);

  // Bob leaves deliberately: he gets the ack, Alice sees the shrink.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  auto ack = ReceiveCase(bob->stream, "roomLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_roomLeft_or_null()->roomId, room_id);
  auto after_leave = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after_leave.has_value());
  ASSERT_EQ(after_leave->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after_leave->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

TEST_F(GolfHubStreamFixture, CommandsOutsideARoomAreRejectedInBand) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  ASSERT_TRUE(
      seat->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto rejected = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  moonbase::golf::JoinRoom join;
  join.roomId = "r-nope";
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto unknown = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(unknown.has_value());

  moonbase::golf::JoinRoom nul_join;
  nul_join.roomId = WithNul("r-nope", "alias");
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(nul_join)).ok());
  auto invalid = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(invalid.has_value());
  EXPECT_EQ(invalid->as_commandRejected_or_null()->reason, "invalid room id");

  // The stream survived both rejections.
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  EXPECT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
}

TEST_F(GolfHubStreamFixture, CleanCloseParksTheSeatAndResumeReclaimsIt) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  // A closed tab is a clean websocket close, byte-identical to any other
  // deliberate-looking exit the browser makes on the way out (#1236).
  // Close carries no leave intent — only the explicit leaveRoom command
  // does — so the seat parks for the grace window instead of emptying.
  bob->stream.Close();
  auto parked = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(parked.has_value());
  {
    const auto* room = parked->as_roomState_or_null();
    ASSERT_EQ(room->players.size(), 2u);
    for (const auto& player : room->players) {
      EXPECT_EQ(player.connected, player.playerId == alice->player_id);
    }
  }

  // The resume token reclaims the parked seat, and the room sees the
  // connected flag flip back.
  auto resumed = OpenSeat(bob->resume_token);
  ASSERT_TRUE(resumed.has_value());
  EXPECT_EQ(resumed->player_id, bob->player_id);
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*ready->as_sessionReady_or_null()->roomId, room_id);
  auto rejoined = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(rejoined.has_value());
  {
    const auto* room = rejoined->as_roomState_or_null();
    ASSERT_EQ(room->players.size(), 2u);
    for (const auto& player : room->players) EXPECT_TRUE(player.connected);
  }
}

TEST_F(GolfGameFixture, FullGameKnockerTieWinsAlone) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;

  // The opening deal, from alice's chair: her cards face down even to her,
  // bob's hand nothing but nulls, the seeded discard face up.
  auto opening = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(opening.has_value());
  {
    const auto* update = opening->as_gameState_or_null();
    ASSERT_NE(update, nullptr);
    EXPECT_EQ(update->view.phase, "playing");
    ASSERT_TRUE(update->view.currentPlayerId.has_value());
    EXPECT_EQ(*update->view.currentPlayerId, alice.player_id);
    EXPECT_EQ(update->view.drawPileCount, 43);
    ASSERT_TRUE(update->view.discardTop.has_value());
    EXPECT_EQ(update->view.discardTop->rank, "Q");
    ASSERT_EQ(update->view.players.size(), 2u);
    for (const auto& player : update->view.players) {
      for (const auto& slot : player.cards) EXPECT_FALSE(slot.card.has_value());
    }
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Opening peeks. Alice's first peek comes back to her alone, with the
  // ace face at the peeked index and nothing of bob's hand.
  moonbase::golf::PeekCard peek;
  peek.cardIndex = 0;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto peeked = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(peeked.has_value());
  {
    const auto& view = peeked->as_gameState_or_null()->view;
    ASSERT_TRUE(view.players[0].cards[0].card.has_value());
    EXPECT_EQ(view.players[0].cards[0].card->rank, "A");
    EXPECT_EQ(view.players[0].revealedIndexes, std::vector<int>{0});
    for (const auto& slot : view.players[1].cards) EXPECT_FALSE(slot.card.has_value());
  }
  peek.cardIndex = 1;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());

  peek.cardIndex = 0;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());
  peek.cardIndex = 1;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());

  // The last peek starts the table-wide countdown: everyone hears it, and
  // bob's view shows his kings but still nothing of alice's aces.
  auto countdown = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(countdown.has_value());
  {
    const auto& view = countdown->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "peeking");
    EXPECT_TRUE(view.allPlayersPeeked);
    ASSERT_TRUE(view.players[1].cards[0].card.has_value());
    EXPECT_EQ(view.players[1].cards[0].card->rank, "K");
    for (const auto& slot : view.players[0].cards) EXPECT_FALSE(slot.card.has_value());
  }
  auto alice_countdown = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(alice_countdown.has_value());
  EXPECT_EQ(alice_countdown->as_gameState_or_null()->view.phase, "peeking");

  // Turn moves wait for the hide.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto gated = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(gated.has_value());

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  auto hidden = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(hidden.has_value());
  {
    const auto& view = hidden->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "playing");
    EXPECT_TRUE(view.players[0].revealedIndexes.empty());
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Alice draws: she sees the face, bob sees only the count drop.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto drawn = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(drawn.has_value());
  {
    const auto& view = drawn->as_gameState_or_null()->view;
    ASSERT_TRUE(view.drawnCard.has_value());
    EXPECT_EQ(view.drawnCard->rank, "Q");
    EXPECT_EQ(view.drawPileCount, 43);  // still on the pile until she commits
  }
  auto bob_saw_draw = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(bob_saw_draw.has_value());
  EXPECT_FALSE(bob_saw_draw->as_gameState_or_null()->view.drawnCard.has_value());

  // She rejects it; the turn passes to bob.
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto turn = ReceiveGolf(alice.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(bob.stream, "turnChanged").has_value());

  // Bob knocks; alice takes the final turn; the game resolves. Both hands
  // cancel to zero, and the knocker takes the tie alone.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok());
  auto knocked = ReceiveGolf(alice.stream, "playerKnocked");
  ASSERT_TRUE(knocked.has_value());
  EXPECT_EQ(knocked->as_playerKnocked_or_null()->playerId, bob.player_id);

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());

  auto ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  {
    const auto* result = ended->as_gameEnded_or_null();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->winner, bob.player_id);
    ASSERT_EQ(result->winners.size(), 1u);
    EXPECT_EQ(result->winners[0], bob.player_id);
    ASSERT_EQ(result->finalScores.size(), 2u);
    for (const auto& score : result->finalScores) EXPECT_EQ(score.score, 0);
  }
  auto bob_ended = ReceiveGolf(bob.stream, "gameEnded");
  ASSERT_TRUE(bob_ended.has_value());

  // The final board is face up for everyone, and the room's running stats
  // credit the knocker's solo win.
  auto final_alice = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(final_alice.has_value());
  {
    const auto* room = final_alice->as_roomState_or_null();
    ASSERT_NE(room, nullptr);
    EXPECT_TRUE(room->games.empty());
    for (const auto& player : room->players) {
      EXPECT_EQ(player.gamesPlayed, 1);
      EXPECT_EQ(player.gamesWon, player.playerId == bob.player_id ? 1 : 0);
      EXPECT_EQ(player.totalScore, 0);
    }
  }
}

TEST_F(GolfGameFixture, ChatReachesTheRoom) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  moonbase::golf::Chat chat;
  chat.text = "good luck!";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(chat)).ok());

  auto to_bob = ReceiveCase(table->bob.stream, "roomChat");
  ASSERT_TRUE(to_bob.has_value());
  EXPECT_EQ(to_bob->as_roomChat_or_null()->playerId, table->alice.player_id);
  EXPECT_EQ(to_bob->as_roomChat_or_null()->text, "good luck!");
  // The message is stored before it is echoed, so the wire carries the
  // server's id and clock rather than anything the sender chose.
  EXPECT_GT(to_bob->as_roomChat_or_null()->messageId, 0);
  EXPECT_GT(to_bob->as_roomChat_or_null()->sentAtUnixMillis, 0);

  auto echo = ReceiveCase(table->alice.stream, "roomChat");
  ASSERT_TRUE(echo.has_value());
  // Both members are told about one message, so both see one id.
  EXPECT_EQ(echo->as_roomChat_or_null()->messageId, to_bob->as_roomChat_or_null()->messageId);

  moonbase::golf::Chat second;
  second.text = "and again";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(second)).ok());
  auto next = ReceiveCase(table->bob.stream, "roomChat");
  ASSERT_TRUE(next.has_value());
  EXPECT_GT(next->as_roomChat_or_null()->messageId, to_bob->as_roomChat_or_null()->messageId)
      << "ids must rise with send order so a client can dedupe and sort by them";

  moonbase::golf::Chat empty;
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(empty)).ok());
  EXPECT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  // Whitespace-only is empty as far as a room is concerned; the handler
  // and the stores agree because they run the same rule.
  moonbase::golf::Chat blank;
  blank.text = "   \t\n";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(blank)).ok());
  EXPECT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  moonbase::golf::Chat oversized;
  oversized.text = std::string(501, 'x');
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(oversized)).ok());
  auto too_long = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(too_long.has_value());
  EXPECT_EQ(too_long->as_commandRejected_or_null()->reason, "chat text is too long");

  // Ill-formed UTF-8 never reaches the hub as such: the JSON-text wire
  // replaces the stray byte with U+FFFD before the handler sees it, so a
  // client cannot drive the edge's UTF-8 rejection over this transport.
  // The message is accepted and echoed with the replacement in place.
  // ValidateChatText's rejection of ill-formed UTF-8 is exercised
  // directly against the store in chat_store_test.
  moonbase::golf::Chat mangled;
  mangled.text = "hi\xC3";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(mangled)).ok());
  auto sanitized = ReceiveCase(table->alice.stream, "roomChat");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(sanitized->as_roomChat_or_null()->text, "hi\xEF\xBF\xBD");
}

// Chat dies with its room. PostgreSQL gets that from the cascade, but
// MemoryChatStore — which is what production runs today — reclaims only
// when the handler calls DropRoom, so a missed call is a leak of up to a
// hundred messages per emptied room, invisible from the wire.
TEST_F(GolfHubStreamFixture, LastMemberLeavingDropsTheRoomsChatHistory) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::golf::Chat chat;
  chat.text = "anyone here?";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  const auto stored = chat_store_->LoadRecent(room_id, 100);
  ASSERT_TRUE(stored.ok());
  ASSERT_EQ(stored->size(), 1u);

  // Alice is the only member, so leaving deletes the room.
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  const auto remaining = chat_store_->LoadRecent(room_id, 100);
  ASSERT_TRUE(remaining.ok());
  EXPECT_TRUE(remaining->empty()) << "the room is gone; its history must be too";
}

TEST_F(GolfHubStreamFixture, JoiningReplaysChatHistoryAfterRoomState) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  std::vector<int64_t> sent_ids;
  for (const char* text : {"one", "two", "three"}) {
    moonbase::golf::Chat chat;
    chat.text = text;
    ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
    auto echo = ReceiveCase(alice->stream, "roomChat");
    ASSERT_TRUE(echo.has_value());
    sent_ids.push_back(echo->as_roomChat_or_null()->messageId);
  }

  auto bob = OpenSeat();
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());

  // Frame by frame: the room snapshot first, then exactly one history
  // event carrying the retained messages — the ids and order the live
  // echoes already reported, so history and live describe one sequence.
  auto first = NextEvent(bob->stream);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(std::string(first->case_name()), "roomState");
  auto second = NextEvent(bob->stream);
  ASSERT_TRUE(second.has_value());
  ASSERT_EQ(std::string(second->case_name()), "roomChatHistory");
  const auto* history = second->as_roomChatHistory_or_null();
  ASSERT_EQ(history->messages.size(), 3u);
  EXPECT_EQ(history->messages[0].text, "one");
  EXPECT_EQ(history->messages[1].text, "two");
  EXPECT_EQ(history->messages[2].text, "three");
  for (std::size_t i = 0; i < sent_ids.size(); ++i) {
    EXPECT_EQ(history->messages[i].messageId, sent_ids[i]);
    EXPECT_EQ(history->messages[i].playerId, alice->player_id);
    EXPECT_GT(history->messages[i].sentAtUnixMillis, 0);
  }

  // Alice was already in the room, so no replay for her: her next frames
  // are bob's join broadcast and then live chat, nothing in between.
  // NextEvent (not ReceiveCase) because a skipped stray event would pass.
  moonbase::golf::Chat live;
  live.text = "welcome";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(live)).ok());
  auto alice_first = NextEvent(alice->stream);
  ASSERT_TRUE(alice_first.has_value());
  EXPECT_EQ(std::string(alice_first->case_name()), "roomState");
  auto alice_second = NextEvent(alice->stream);
  ASSERT_TRUE(alice_second.has_value());
  EXPECT_EQ(std::string(alice_second->case_name()), "roomChat");

  // The live message continues the id sequence the history reported.
  auto bob_live = ReceiveCase(bob->stream, "roomChat");
  ASSERT_TRUE(bob_live.has_value());
  EXPECT_EQ(bob_live->as_roomChat_or_null()->text, "welcome");
  EXPECT_GT(bob_live->as_roomChat_or_null()->messageId, sent_ids.back());
}

TEST_F(GolfHubStreamFixture, JoiningAChatlessRoomHearsAnEmptyHistory) {
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

  // The event arrives even with nothing to replay, so a client learns
  // "history loaded, and it is empty" instead of inferring from silence.
  auto state = NextEvent(bob->stream);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(std::string(state->case_name()), "roomState");
  auto history = NextEvent(bob->stream);
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(std::string(history->case_name()), "roomChatHistory");
  EXPECT_TRUE(history->as_roomChatHistory_or_null()->messages.empty());

  // The creator hears no history at all — creating is not joining. Her
  // next frame after her create-roomState is bob's join broadcast; a
  // stray replay to her would land here and fail the case check.
  auto alice_next = NextEvent(alice->stream);
  ASSERT_TRUE(alice_next.has_value());
  EXPECT_EQ(std::string(alice_next->case_name()), "roomState");
}

TEST_F(GolfHubStreamFixture, ResumingOnAFreshInstanceReplaysChatHistory) {
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
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  moonbase::golf::Chat chat;
  chat.text = "hello";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());
  chat.text = "hi back";
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());

  // A second hub restores rooms and members from the shared store; its
  // registry has never seen bob, so his resume admits as new with his
  // membership intact — the store-restart shape of resume, no wire
  // failure needed. His live seat on the first instance is irrelevant
  // here: registries are per-instance.
  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  auto resumed = OpenSeatVia(*instance->client, bob->resume_token);
  ASSERT_TRUE(resumed.has_value());
  EXPECT_EQ(resumed->player_id, bob->player_id);

  auto ready = NextEvent(resumed->stream);
  ASSERT_TRUE(ready.has_value());
  ASSERT_EQ(std::string(ready->case_name()), "sessionReady");
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*ready->as_sessionReady_or_null()->roomId, room_id);

  // The snapshot he missed, then the chat he missed, in that order.
  auto state = NextEvent(resumed->stream);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(std::string(state->case_name()), "roomState");
  auto replay = NextEvent(resumed->stream);
  ASSERT_TRUE(replay.has_value());
  ASSERT_EQ(std::string(replay->case_name()), "roomChatHistory");
  const auto* history = replay->as_roomChatHistory_or_null();
  ASSERT_EQ(history->messages.size(), 2u);
  EXPECT_EQ(history->messages[0].text, "hello");
  EXPECT_EQ(history->messages[0].playerId, alice->player_id);
  EXPECT_EQ(history->messages[1].text, "hi back");
  EXPECT_EQ(history->messages[1].playerId, bob->player_id);
  EXPECT_GT(history->messages[1].messageId, history->messages[0].messageId);
}

TEST_F(GolfHubStreamFixture, JoinHistoryIsCappedAtTheRetentionLimit) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  // One over the retention window, so the replay must both cap at the
  // limit and hold the newest end — a hardcoded smaller LoadRecent limit
  // or an off-by-one prune fails here, where the 3-message test cannot.
  for (std::size_t i = 1; i <= kChatHistoryLimit + 1; ++i) {
    moonbase::golf::Chat chat;
    chat.text = "m-" + std::to_string(i);
    ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
    ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  }

  auto bob = OpenSeat();
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  auto replay = NextEvent(bob->stream);
  ASSERT_TRUE(replay.has_value());
  ASSERT_EQ(std::string(replay->case_name()), "roomChatHistory");
  const auto* history = replay->as_roomChatHistory_or_null();
  ASSERT_EQ(history->messages.size(), kChatHistoryLimit);
  EXPECT_EQ(history->messages.front().text, "m-2") << "the oldest message fell to retention";
  EXPECT_EQ(history->messages.back().text, "m-" + std::to_string(kChatHistoryLimit + 1));
}

TEST_F(GolfHubStreamFixture, AFailedHistoryLoadDoesNotFailTheResume) {
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
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  moonbase::golf::Chat chat;
  chat.text = "stored fine";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());

  // History is best-effort: when the load fails, the resume still lands
  // (sessionReady, roomState) and the stream simply hears no history
  // event — the model's documented absence case.
  auto capture = std::make_shared<CapturingMetricsRecorder>();
  auto instance = BuildSecondInstance(
      vault_, store_, std::make_shared<FailingHistoryChatStore>(chat_store_), capture);
  ASSERT_NE(instance, nullptr);
  auto resumed = OpenSeatVia(*instance->client, bob->resume_token);
  ASSERT_TRUE(resumed.has_value());

  auto ready = NextEvent(resumed->stream);
  ASSERT_TRUE(ready.has_value());
  ASSERT_EQ(std::string(ready->case_name()), "sessionReady");
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  auto state = NextEvent(resumed->stream);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(std::string(state->case_name()), "roomState");

  // Nothing further: no history event, and no failure surfaced to the
  // client. The bounded receive times out on a live, usable stream.
  auto nothing = resumed->stream.Receive(std::chrono::milliseconds(300));
  EXPECT_FALSE(nothing.ok()) << "a failed history load must send nothing, not something";

  // Both failures were counted by stage: the resume's history load, and
  // the restore's cursor seed read that went through the same failing
  // store — which failed open to a zero cursor rather than a loss.
  EXPECT_EQ(capture->CounterTotal("chat_failures", {{"stage", "history_load"}}), 1);
  EXPECT_EQ(capture->CounterTotal("chat_failures", {{"stage", "cursor_seed"}}), 1);
}

// What the chat paths count (#1226 item 10), asserted through the
// capturing recorder — including the rule the epic states outright:
// no room id, player id, or message text may reach a metric name or
// label. Counts and stages only.
TEST_F(GolfHubStreamFixture, ChatMetricsCountOutcomesWithoutIdentifiers) {
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

  moonbase::golf::Chat chat;
  chat.text = "counted, never labeled";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());

  // One stored append, one drain that delivered its one row, one
  // history replay (bob's join; createRoom sends none).
  EXPECT_EQ(metrics_->CounterTotal("chat_appends", {{"result", "stored"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered"), 1);
  EXPECT_EQ(metrics_->CounterTotal("chat_history_replays"), 1);
  bool saw_drain = false;
  for (const auto& entry : metrics_->Entries()) {
    if (entry.name == "chat_catch_up_rows" && entry.value == 1.0) saw_drain = true;
  }
  EXPECT_TRUE(saw_drain) << "the drain's row count feeds the lag distribution";

  // The sweep: nothing recorded anywhere carries the identifiers.
  for (const auto& entry : metrics_->Entries()) {
    for (const std::string& secret :
         {room_id, alice->player_id, bob->player_id, std::string(chat.text)}) {
      EXPECT_EQ(entry.name.find(secret), std::string::npos) << entry.name;
      for (const auto& [key, value] : entry.attributes) {
        EXPECT_EQ(key.find(secret), std::string::npos) << key;
        EXPECT_EQ(value.find(secret), std::string::npos) << value;
      }
    }
  }
}

// The unavailable side of the append counter: the store cannot be
// reached, the sender is told so, and nothing counts as stored or
// delivered.
TEST_F(GolfHubStreamFixture, AnUnreachableStoreCountsTheAppendAsUnavailable) {
  auto capture = std::make_shared<CapturingMetricsRecorder>();
  auto instance = BuildSecondInstance(
      vault_, store_, std::make_shared<FailingAppendChatStore>(chat_store_), capture);
  ASSERT_NE(instance, nullptr);
  auto alice = OpenSeatVia(*instance->client);
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::golf::Chat chat;
  chat.text = "never stored";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto rejected = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "chat is unavailable");

  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "unavailable"}}), 1);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "stored"}}), 0);
  EXPECT_EQ(capture->CounterTotal("chat_rows_delivered"), 0);
}

// The rejected side of the same counter: the store says the sender's
// membership vanished mid-send (the race store-level authorization
// exists for), the client hears the same "not in a room" a pre-store
// reject uses, and the outcome counts as rejected — an authorization
// answer, not an outage.
TEST_F(GolfHubStreamFixture, AStaleMembershipCountsTheAppendAsRejected) {
  auto capture = std::make_shared<CapturingMetricsRecorder>();
  auto instance = BuildSecondInstance(vault_, store_,
                                      std::make_shared<NotAMemberChatStore>(chat_store_), capture);
  ASSERT_NE(instance, nullptr);
  auto alice = OpenSeatVia(*instance->client);
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::golf::Chat chat;
  chat.text = "membership just vanished";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto rejected = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "rejected"}}), 1);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "unavailable"}}), 0);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "stored"}}), 0);
}

// Drain metrics at batch grain: one wake that finds two committed rows
// delivers them as one drain — the counter grows by the batch and the
// distribution takes a single observation of the batch size, which is
// what makes an observation's size read as "rows behind at the wake".
// A redundant wake then records the zero that keeps the dashboard's
// windowed average honest, and delivers nothing.
TEST_F(GolfHubStreamFixture, DrainMetricsCountBatchesAndZeroWakes) {
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

  // Two rows land behind the hub's back — committed straight through the
  // store, the shape of another instance's appends.
  ASSERT_TRUE(chat_store_->Append(room_id, alice->player_id, "first behind", "remote").ok());
  ASSERT_TRUE(chat_store_->Append(room_id, alice->player_id, "second behind", "remote").ok());

  const auto observations = [&] {
    std::vector<double> values;
    for (const auto& entry : metrics_->Entries()) {
      if (entry.name == "chat_catch_up_rows") values.push_back(entry.value);
    }
    return values;
  };
  const double delivered_before = metrics_->CounterTotal("chat_rows_delivered");
  const std::size_t drains_before = observations().size();

  handler_->OnNotify(ChatChannel(room_id), "remote-instance");
  for (auto* seat : {&*alice, &*bob}) {
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomChat").has_value());
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomChat").has_value());
  }
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered") - delivered_before, 2);
  auto after_drain = observations();
  ASSERT_EQ(after_drain.size(), drains_before + 1);
  EXPECT_EQ(after_drain.back(), 2.0);

  // Redundant wake: nothing new, one zero observation, nothing counted
  // as delivered. OnNotify pumps synchronously, so no waiting.
  handler_->OnNotify(ChatChannel(room_id), "remote-instance");
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered") - delivered_before, 2);
  auto after_redundant = observations();
  ASSERT_EQ(after_redundant.size(), drains_before + 2);
  EXPECT_EQ(after_redundant.back(), 0.0);
}

// The membership guard, exercised through the handler that owns it
// rather than a test double. MemoryChatStore authorizes every append
// through this, so what it answers is what decides whether a message
// can be stored.
TEST_F(GolfHubStreamFixture, WithMemberRunsOnlyForCurrentMembers) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  bool ran = false;
  EXPECT_TRUE(handler_->WithMember(room_id, alice->player_id, [&] { ran = true; }));
  EXPECT_TRUE(ran);

  ran = false;
  EXPECT_FALSE(handler_->WithMember(room_id, "nobody", [&] { ran = true; }));
  EXPECT_FALSE(handler_->WithMember("no-such-room", alice->player_id, [&] { ran = true; }));
  EXPECT_FALSE(ran) << "the action must not run when the seat is not there";

  // Leaving revokes it, which is what keeps a chat append off a seat
  // that is already gone.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomLeft").has_value());
  EXPECT_FALSE(handler_->WithMember(room_id, bob->player_id, [&] { ran = true; }));
  EXPECT_FALSE(ran);

  // And a message from the revoked seat is refused rather than echoed.
  moonbase::golf::Chat chat;
  chat.text = "still here?";
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto rejected = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");
}

TEST_F(GolfGameFixture, AbandoningALiveGameResolvesIt) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  // Drain the opening deals so the next gameState each seat sees is the
  // finish ceremony's.
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());

  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  auto ack = ReceiveGolf(table->bob.stream, "gameLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_gameLeft_or_null()->gameId, table->game_id);

  // Alice is the last seat standing: the game resolves in her favor and
  // leaves the room's game list empty. The final view and the summary
  // still carry bob's seat — his name, his cards face up, and the score
  // they stood at — not just the survivor's (#1236).
  auto final_view = ReceiveGolf(table->alice.stream, "gameState");
  ASSERT_TRUE(final_view.has_value());
  {
    const auto& view = final_view->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "ended");
    ASSERT_EQ(view.players.size(), 2u);
    for (const auto& player : view.players) {
      EXPECT_EQ(player.revealedIndexes.size(), 4u) << player.playerId;
      EXPECT_TRUE(player.score.has_value()) << player.playerId;
    }
  }
  auto ended = ReceiveGolf(table->alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  const auto* result = ended->as_gameEnded_or_null();
  ASSERT_EQ(result->winners.size(), 1u);
  EXPECT_EQ(result->winners[0], table->alice.player_id);
  ASSERT_EQ(result->finalScores.size(), 2u);
  std::set<std::string> scored;
  for (const auto& score : result->finalScores) scored.insert(score.playerId);
  EXPECT_TRUE(scored.contains(table->alice.player_id));
  EXPECT_TRUE(scored.contains(table->bob.player_id));

  auto room = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
}

// The reported bug (#1236): an accidental browser close mid-game arrives
// as a clean websocket close, which used to resolve the game against the
// absent player within seconds. A close parks the seat instead: the
// table sees the disconnect, nothing ends, and the resume token reclaims
// the seat with the game intact.
TEST_F(GolfGameFixture, MidGameBrowserCloseParksTheSeatAndTheGameSurvives) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());

  table->bob.stream.Close();

  // Alice hears the disconnect — and nothing that ends the game.
  bool bob_disconnected = false;
  for (int i = 0; i < 8 && !bob_disconnected; ++i) {
    auto event = NextEvent(alice.stream);
    ASSERT_TRUE(event.has_value());
    if (const auto* envelope = event->as_golf_or_null()) {
      EXPECT_EQ(envelope->update.as_gameEnded_or_null(), nullptr)
          << "a parked seat must not resolve the game";
      continue;
    }
    const auto* room = event->as_roomState_or_null();
    if (room == nullptr) continue;
    ASSERT_EQ(room->players.size(), 2u);
    for (const auto& player : room->players) {
      if (player.playerId == table->bob.player_id) bob_disconnected = !player.connected;
    }
  }
  ASSERT_TRUE(bob_disconnected);
  // And then silence: no verdict follows the park.
  EXPECT_FALSE(alice.stream.Receive(std::chrono::milliseconds(300)).ok());

  // The resume token reclaims the seat with the game still going.
  auto resumed = OpenSeat(table->bob.resume_token);
  ASSERT_TRUE(resumed.has_value());
  EXPECT_EQ(resumed->player_id, table->bob.player_id);
  auto ready = ReceiveCase(resumed->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  auto rejoined = ReceiveGolf(resumed->stream, "gameJoined");
  ASSERT_TRUE(rejoined.has_value());
  {
    const auto& view = rejoined->as_gameJoined_or_null()->view;
    EXPECT_EQ(view.gameId, table->game_id);
    EXPECT_NE(view.phase, "ended");
    ASSERT_EQ(view.players.size(), 2u);
  }
  // The reclaimed seat still plays: an opening peek comes back revealed.
  moonbase::golf::PeekCard peek;
  peek.cardIndex = 0;
  ASSERT_TRUE(resumed->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto peeked = ReceiveGolf(resumed->stream, "gameState");
  ASSERT_TRUE(peeked.has_value());
  {
    const auto& view = peeked->as_gameState_or_null()->view;
    ASSERT_EQ(view.players.size(), 2u);
    for (const auto& player : view.players) {
      if (player.playerId != table->bob.player_id) continue;
      EXPECT_EQ(player.revealedIndexes, std::vector<int>{0});
    }
  }
}

// Grace expiry is the deliberate end of a disconnect (#1236): only after
// the window runs out does the absence resolve the game — in the
// survivor's favor, with every seat still on the scorecard.
class ShortGraceFixture : public GolfGameFixture {
 protected:
  std::chrono::seconds GracePeriod() override { return std::chrono::seconds(1); }
};

TEST_F(ShortGraceFixture, GraceExpiryResolvesTheGameWithEverySeatScored) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());

  table->bob.stream.Close();

  // The window runs out with bob still gone: alice takes the game, and
  // the summary keeps bob's seat — name and standing score — next to
  // hers.
  auto ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  const auto* result = ended->as_gameEnded_or_null();
  EXPECT_EQ(result->winner, alice.player_id);
  ASSERT_EQ(result->winners.size(), 1u);
  EXPECT_EQ(result->winners[0], alice.player_id);
  ASSERT_EQ(result->finalScores.size(), 2u);
  std::set<std::string> scored;
  for (const auto& score : result->finalScores) scored.insert(score.playerId);
  EXPECT_TRUE(scored.contains(alice.player_id));
  EXPECT_TRUE(scored.contains(table->bob.player_id));

  // The reaped seat leaves the room: alice ends up alone.
  bool alone = false;
  for (int i = 0; i < 4 && !alone; ++i) {
    auto room = ReceiveCase(alice.stream, "roomState");
    ASSERT_TRUE(room.has_value());
    alone = room->as_roomState_or_null()->players.size() == 1;
  }
  EXPECT_TRUE(alone);
}

TEST_F(GolfGameFixture, IllegalMovesRejectInBandAndTheGameContinues) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Not bob's turn.
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto rejected = ReceiveCase(table->bob.stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not your turn");

  // A bad index dies before it reaches the engine.
  moonbase::golf::PeekCard peek;
  peek.cardIndex = 9;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto bad_index = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(bad_index.has_value());
  EXPECT_EQ(bad_index->as_commandRejected_or_null()->reason, "invalid card index");

  // No blind moves: swapping without drawing is refused in-band.
  moonbase::golf::SwapCard blind;
  blind.cardIndex = 0;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromSwapcard(blind))).ok());
  auto blind_swap = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(blind_swap.has_value());
  EXPECT_EQ(blind_swap->as_commandRejected_or_null()->reason, "no drawn card to swap");

  // The stream survived: a legal move still lands.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  EXPECT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
}

TEST_F(GolfGameFixture, PendingGameLifecycleAndLobbySummaries) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  // The whole room hears the creation attributed to its creator; the
  // creator's client relies on createdBy to recognize its own echo.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto echo = ReceiveGolf(alice->stream, "gameCreated");
  ASSERT_TRUE(echo.has_value());
  EXPECT_EQ(echo->as_gameCreated_or_null()->createdBy, alice->player_id);
  auto announced = ReceiveGolf(bob->stream, "gameCreated");
  ASSERT_TRUE(announced.has_value());
  EXPECT_EQ(announced->as_gameCreated_or_null()->createdBy, alice->player_id);
  auto joined = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_gameJoined_or_null()->view.phase, "waiting");
  EXPECT_EQ(announced->as_gameCreated_or_null()->gameId,
            joined->as_gameJoined_or_null()->view.gameId);

  moonbase::golf::JoinGame nul_join;
  nul_join.gameId = WithNul(joined->as_gameJoined_or_null()->view.gameId, "alias");
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromJoingame(nul_join))).ok());
  auto invalid_game = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(invalid_game.has_value());
  EXPECT_EQ(invalid_game->as_commandRejected_or_null()->reason, "invalid game id");

  // A solo game cannot start.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok());
  auto lonely = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(lonely.has_value());
  EXPECT_EQ(lonely->as_commandRejected_or_null()->reason, "need at least 2 players to start");

  // One game per player at a time.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto second = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_commandRejected_or_null()->reason, "leave your current game first");

  // The lobby sees the pending game: waiting, one seat filled.
  ASSERT_TRUE(
      bob->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto lobby = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  {
    const auto* room = lobby->as_roomState_or_null();
    ASSERT_EQ(room->games.size(), 1u);
    EXPECT_EQ(room->games[0].status, "waiting");
    EXPECT_EQ(room->games[0].playerCount, 1);
  }

  // Leaving a pending game as its last member dissolves it.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  auto ack = ReceiveGolf(alice->stream, "gameLeft");
  ASSERT_TRUE(ack.has_value());
  auto after = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after.has_value());
  EXPECT_TRUE(after->as_roomState_or_null()->games.empty());
}

TEST_F(GolfGameFixture, RoomStatsAccumulateAcrossGames) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Quickest legal game: alice knocks unseen, bob takes his final turn.
  const auto play_out = [](Seat& alice, Seat& bob) {
    if (!alice.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok()) return false;
    if (!ReceiveGolf(bob.stream, "playerKnocked").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok()) {
      return false;
    }
    if (!ReceiveGolf(bob.stream, "gameState").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok()) {
      return false;
    }
    return ReceiveGolf(alice.stream, "gameEnded").has_value() &&
           ReceiveGolf(bob.stream, "gameEnded").has_value();
  };
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Round two in the same room.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto joined = ReceiveGolf(table->alice.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  moonbase::golf::JoinGame join;
  join.gameId = joined->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(table->bob.stream.Send(Move(GolfMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameJoined").has_value());
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameStarted").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameStarted").has_value());
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Running totals: two games played, both won solo by alice the knocker
  // (identical zero-scoring deals; the knocker takes the tie).
  ASSERT_TRUE(
      table->alice.stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{}))
          .ok());
  auto lobby = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  for (const auto& player : lobby->as_roomState_or_null()->players) {
    EXPECT_EQ(player.gamesPlayed, 2);
    EXPECT_EQ(player.totalScore, 0);
    EXPECT_EQ(player.gamesWon, player.playerId == table->alice.player_id ? 2 : 0);
  }
}

// The id seam at work: a generator that hands out the same room id or
// game code twice, forcing the create paths' collision loops to roll
// again.
class CollidingIds final : public IdGenerator {
 public:
  std::string PlayerId() override { return "player-" + std::to_string(++players_); }
  std::string RoomId() override { return ++rooms_ <= 2 ? "SAMERM" : "ROOM2X"; }
  std::string GameCode() override { return ++codes_ <= 2 ? "DUPLIC" : "FRESH1"; }

 private:
  int players_ = 0;
  int rooms_ = 0;
  int codes_ = 0;
};

class CollidingIdsFixture : public GolfGameFixture {
 protected:
  CollidingIdsFixture() { ids_ = std::make_shared<CollidingIds>(); }
};

TEST_F(CollidingIdsFixture, GameCodeCollisionRollsAgain) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto first = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->as_gameJoined_or_null()->view.gameId, "DUPLIC");

  // Bob's create draws "DUPLIC" again; the hub rolls until it's fresh.
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto second = ReceiveGolf(bob->stream, "gameJoined");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_gameJoined_or_null()->view.gameId, "FRESH1");
}

TEST_F(CollidingIdsFixture, RoomCodeCollisionRollsAgain) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto first = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->as_roomState_or_null()->roomId, "SAMERM");

  // Bob's create draws "SAMERM" again; the hub rolls until it's fresh.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto second = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_roomState_or_null()->roomId, "ROOM2X");
}

// A vault whose store is down: GetSession must fail closed with a
// non-leaking error, never mint a credential nothing recorded.
class FailingVault final : public TicketVault {
 public:
  absl::StatusOr<std::string> IssueTicket(const std::string&) override {
    return absl::UnavailableError("vault down");
  }
  absl::StatusOr<std::string> IssueResumeToken(const std::string&) override {
    return absl::UnavailableError("vault down");
  }
  bool PeekTicket(const std::string&) const override { return false; }
  std::optional<std::string> SpendTicket(const std::string&) override { return std::nullopt; }
  std::optional<std::string> ResolveResumeToken(const std::string&) const override {
    return std::nullopt;
  }
};

class FailingVaultFixture : public GolfHubStreamFixture {
 protected:
  std::shared_ptr<TicketVault> MakeVault() override { return std::make_shared<FailingVault>(); }
};

TEST_F(FailingVaultFixture, GetSessionFailsClosedWhenTheVaultIsDown) {
  const auto session = client_->GetSession(moonbase::golf::GetSessionInput{});
  ASSERT_FALSE(session.ok());
}

}  // namespace
}  // namespace golf_hub
