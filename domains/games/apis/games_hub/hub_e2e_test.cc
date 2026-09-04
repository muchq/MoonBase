// Phase 1 e2e: session minting, ticket admission, room lifecycle, and the
// reconnect-adjacent seat semantics, driven through the generated client
// over the in-memory pair. Wire-level details (JSON-text framing, real
// sockets) are upstream-tested; these pin the hub's behavior.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/chat_store.h"
#include "domains/games/apis/games_hub/stream_test_fixture.h"

namespace games_hub {
namespace {

using moonbase::games::GolfCommands;
using moonbase::games::GolfEvents;

using moonbase::games::GolfMove;
using moonbase::games::GolfUpdate;

std::string WithNul(std::string prefix, std::string suffix) {
  prefix.push_back('\0');
  prefix.append(suffix);
  return prefix;
}

// The forfeit rule, pinned directly: an abandonment end is scored among
// roster seats only, so the departed hand loses however well it stands.
TEST(WinnersAmong, AbandonerWithTheBetterScoreStillLoses) {
  using cards::Card;
  using cards::Rank;
  using cards::Suit;
  // The abandoner's four threes pair-cancel to zero; the survivor holds
  // an unpaired 2+3+4+5. The engine's own tally crowns the departed
  // seat — it cannot know who left.
  const golf::Player survivor{"andy", Card(Suit::Clubs, Rank::Two),
                              Card(Suit::Diamonds, Rank::Three), Card(Suit::Hearts, Rank::Four),
                              Card(Suit::Spades, Rank::Five)};
  const golf::Player abandoner{"mercy", Card(Suit::Clubs, Rank::Three),
                               Card(Suit::Diamonds, Rank::Three), Card(Suit::Hearts, Rank::Three),
                               Card(Suit::Spades, Rank::Three)};
  const golf::GameState state{{Card{Suit::Diamonds, Rank::Ten}},
                              {Card{Suit::Hearts, Rank::Four}},
                              {survivor, abandoner},
                              false,
                              /*_whoseTurn=*/0,
                              golf::GameState::kAbandoned,
                              "game",
                              "v"};
  ASSERT_TRUE(state.isOver());
  ASSERT_LT(state.getPlayer(1).score(), state.getPlayer(0).score());
  EXPECT_EQ(state.winners(), (std::unordered_set<int>{1}));

  // The roster no longer names the abandoner: the survivor wins the
  // forfeit despite the worse hand.
  EXPECT_EQ(WinnersAmong(state, {"andy"}), (std::unordered_set<int>{0}));
}

// With every seat still on the roster this is exactly the engine's
// rule — the knocker takes ties alone.
TEST(WinnersAmong, OrdinaryFinishMatchesTheEngineKnockerTiesIncluded) {
  using cards::Card;
  using cards::Rank;
  using cards::Suit;
  const golf::Player andy{"andy", Card(Suit::Clubs, Rank::Two), Card(Suit::Diamonds, Rank::Two),
                          Card(Suit::Hearts, Rank::Two), Card(Suit::Spades, Rank::Two)};
  const golf::Player mercy{"mercy", Card(Suit::Clubs, Rank::Three),
                           Card(Suit::Diamonds, Rank::Three), Card(Suit::Hearts, Rank::Three),
                           Card(Suit::Spades, Rank::Three)};
  // Both hands cancel to zero; mercy knocked and the turn came back
  // around, ending the game with the tie hers alone.
  const golf::GameState state{{Card{Suit::Diamonds, Rank::Ten}},
                              {Card{Suit::Hearts, Rank::Four}},
                              {andy, mercy},
                              false,
                              /*_whoseTurn=*/1,
                              /*_whoKnocked=*/1,
                              "game",
                              "v"};
  ASSERT_TRUE(state.isOver());
  EXPECT_EQ(WinnersAmong(state, {"andy", "mercy"}), state.winners());
  EXPECT_EQ(WinnersAmong(state, {"andy", "mercy"}), (std::unordered_set<int>{1}));
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
  std::shared_ptr<GolfHub> golf;
  std::shared_ptr<CapturingMetricsRecorder> metrics;
  std::unique_ptr<moonbase::games::GamesHubServer> server;
  std::unique_ptr<moonbase::games::GamesHubClient> client;
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions;

  // The sweep the fixture's TearDown runs for its own recorder, run here for
  // this instance's — paths that only ever fire on a second instance (the
  // store-level chat rejections, some chat_failures stages) otherwise sit
  // outside the emit→declare check entirely (#1327).
  ~SecondInstance() {
    for (auto& session : sessions) session->Close();
    ExpectOnlyDeclaredCounterSeries(*metrics);
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
  absl::StatusOr<std::vector<ChatRow>> LoadRecent([[maybe_unused]] const std::string& room_id,
                                                  [[maybe_unused]] std::size_t limit) override {
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

  absl::StatusOr<ChatRow> Append([[maybe_unused]] const std::string& room_id,
                                 [[maybe_unused]] const std::string& player_id,
                                 [[maybe_unused]] const std::string& text,
                                 [[maybe_unused]] const std::string& notify_payload) override {
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

  absl::StatusOr<ChatRow> Append([[maybe_unused]] const std::string& room_id,
                                 [[maybe_unused]] const std::string& player_id,
                                 [[maybe_unused]] const std::string& text,
                                 [[maybe_unused]] const std::string& notify_payload) override {
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
    std::shared_ptr<CapturingMetricsRecorder> metrics = MakeCapturingMetricsRecorder(),
    std::chrono::seconds grace = std::chrono::seconds(60)) {
  auto instance = std::make_unique<SecondInstance>();
  instance->metrics = std::move(metrics);
  auto ids = std::make_shared<RemoteIdGenerator>();
  instance->golf = std::make_shared<GolfHub>(vault, std::make_shared<cards::NoShuffleDealer>(), ids,
                                             grace, instance->metrics, std::move(store),
                                             std::move(chat_store), UnlimitedRateLimits());
  EXPECT_TRUE(instance->golf->RestoreFromStore().ok());
  instance->server =
      std::make_unique<moonbase::games::GamesHubServer>(std::make_shared<GamesHubHandler>(
          vault, ids, instance->golf, std::make_shared<ThoughtsHub>(vault, instance->metrics)));

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
  auto client = moonbase::games::GamesHubClient::Create(std::move(config));
  EXPECT_TRUE(client.ok());
  if (!client.ok()) return nullptr;
  instance->client = std::make_unique<moonbase::games::GamesHubClient>(std::move(*client));
  return instance;
}

}  // namespace

class GolfGameFixture : public GamesHubStreamFixture {};

namespace {

TEST_F(GamesHubStreamFixture, SessionMintsDistinctPlayersAndResumeTokenRoundTrips) {
  moonbase::games::GetSessionInput input;
  auto first = client_->GetSession(input);
  auto second = client_->GetSession(input);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->playerId, second->playerId);

  moonbase::games::GetSessionInput resume;
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

class ProtocolBoundaryFixture : public GamesHubStreamFixture {
 protected:
  std::shared_ptr<TicketVault> MakeVault() override {
    recording_vault_ = std::make_shared<RecordingVault>();
    return recording_vault_;
  }

  std::shared_ptr<RecordingVault> recording_vault_;
};

TEST_F(ProtocolBoundaryFixture, NulBearingCredentialsNeverReachTheVault) {
  moonbase::games::GetSessionInput session;
  session.resumeToken = WithNul("rt-bogus", "suffix");
  const auto minted = client_->GetSession(session);
  ASSERT_TRUE(minted.ok());
  EXPECT_EQ(recording_vault_->resolve_calls.load(), 0);

  moonbase::games::PlayInput play;
  play.ticket = WithNul("t-bogus", "suffix");
  auto stream = client_->Play(play);
  ASSERT_TRUE(stream.ok());
  const auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated");
  EXPECT_EQ(recording_vault_->spend_calls.load(), 0);
}

TEST_F(GamesHubStreamFixture, BadTicketFailsTypedBeforeAnyEvent) {
  moonbase::games::PlayInput input;
  input.ticket = "t-bogus";
  auto stream = client_->Play(input);
  ASSERT_TRUE(stream.ok()) << stream.error().message();
  auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated") << first.error().message();
}

TEST_F(GamesHubStreamFixture, TicketSpendsOnce) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // The same ticket is gone; a second dial with it dies typed. (A fresh
  // ticket for the same player is the SeatConflict test below.)
  moonbase::games::PlayInput replay;
  replay.ticket = "t-bogus";  // any unspendable ticket behaves alike
  auto second = client_->Play(replay);
  ASSERT_TRUE(second.ok());
  auto first_event = second->Receive();
  ASSERT_FALSE(first_event.ok());
  EXPECT_EQ(first_event.error().code(), "Unauthenticated");
}

TEST_F(GamesHubStreamFixture, SecondLiveConnectionForSamePlayerIsRefused) {
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

TEST_F(GamesHubStreamFixture, CreateJoinAndLeaveBroadcastRoomState) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const auto* room = created->as_roomState_or_null();
  ASSERT_NE(room, nullptr);
  ASSERT_EQ(room->players.size(), 1u);
  EXPECT_EQ(room->players[0].playerId, alice->player_id);
  const std::string room_id = room->roomId;

  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto bob_view = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(bob_view.has_value());
  EXPECT_EQ(bob_view->as_roomState_or_null()->players.size(), 2u);
  auto alice_view = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(alice_view.has_value());
  EXPECT_EQ(alice_view->as_roomState_or_null()->players.size(), 2u);

  // Bob leaves deliberately: he gets the ack, Alice sees the shrink.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  auto ack = ReceiveCase(bob->stream, "roomLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_roomLeft_or_null()->roomId, room_id);
  auto after_leave = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after_leave.has_value());
  ASSERT_EQ(after_leave->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after_leave->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

TEST_F(GamesHubStreamFixture, CommandsOutsideARoomAreRejectedInBand) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  ASSERT_TRUE(
      seat->stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto rejected = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  moonbase::games::JoinRoom join;
  join.roomId = "r-nope";
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto unknown = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(unknown.has_value());

  moonbase::games::JoinRoom nul_join;
  nul_join.roomId = WithNul("r-nope", "alias");
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(nul_join)).ok());
  auto invalid = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(invalid.has_value());
  EXPECT_EQ(invalid->as_commandRejected_or_null()->reason, "invalid room id");

  // The stream survived both rejections.
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  EXPECT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
}

TEST_F(GamesHubStreamFixture, CleanCloseParksTheSeatAndResumeReclaimsIt) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::games::JoinRoom join;
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
  moonbase::games::PeekCard peek;
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
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  auto gated = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(gated.has_value());

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::games::HideCards{}))).ok());
  auto hidden = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(hidden.has_value());
  {
    const auto& view = hidden->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "playing");
    EXPECT_TRUE(view.players[0].revealedIndexes.empty());
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Alice draws: she sees the face, bob sees only the count drop.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
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
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());
  auto turn = ReceiveGolf(alice.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(bob.stream, "turnChanged").has_value());

  // Bob knocks; alice takes the final turn; the game resolves. Both hands
  // cancel to zero, and the knocker takes the tie alone.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok());
  auto knocked = ReceiveGolf(alice.stream, "playerKnocked");
  ASSERT_TRUE(knocked.has_value());
  EXPECT_EQ(knocked->as_playerKnocked_or_null()->playerId, bob.player_id);

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok());

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

  moonbase::games::Chat chat;
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

  moonbase::games::Chat second;
  second.text = "and again";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(second)).ok());
  auto next = ReceiveCase(table->bob.stream, "roomChat");
  ASSERT_TRUE(next.has_value());
  EXPECT_GT(next->as_roomChat_or_null()->messageId, to_bob->as_roomChat_or_null()->messageId)
      << "ids must rise with send order so a client can dedupe and sort by them";

  moonbase::games::Chat empty;
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(empty)).ok());
  EXPECT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  // Whitespace-only is empty as far as a room is concerned; the handler
  // and the stores agree because they run the same rule.
  moonbase::games::Chat blank;
  blank.text = "   \t\n";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(blank)).ok());
  EXPECT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  moonbase::games::Chat oversized;
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
  moonbase::games::Chat mangled;
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
TEST_F(GamesHubStreamFixture, LastMemberLeavingDropsTheRoomsChatHistory) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::Chat chat;
  chat.text = "anyone here?";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  const auto stored = chat_store_->LoadRecent(room_id, 100);
  ASSERT_TRUE(stored.ok());
  ASSERT_EQ(stored->size(), 1u);

  // Alice is the only member, so leaving deletes the room.
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  const auto remaining = chat_store_->LoadRecent(room_id, 100);
  ASSERT_TRUE(remaining.ok());
  EXPECT_TRUE(remaining->empty()) << "the room is gone; its history must be too";
}

TEST_F(GamesHubStreamFixture, JoiningReplaysChatHistoryAfterRoomState) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  std::vector<int64_t> sent_ids;
  for (const char* text : {"one", "two", "three"}) {
    moonbase::games::Chat chat;
    chat.text = text;
    ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
    auto echo = ReceiveCase(alice->stream, "roomChat");
    ASSERT_TRUE(echo.has_value());
    sent_ids.push_back(echo->as_roomChat_or_null()->messageId);
  }

  auto bob = OpenSeat();
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join;
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
  moonbase::games::Chat live;
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

TEST_F(GamesHubStreamFixture, JoiningAChatlessRoomHearsAnEmptyHistory) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::games::JoinRoom join;
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

TEST_F(GamesHubStreamFixture, ResumingOnAFreshInstanceReplaysChatHistory) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  moonbase::games::Chat chat;
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

// The boot-time twin of ActiveSignalRefreshesHeldRoom's contract: a
// channel-active with nothing new in the rows projects nothing. A fresh
// instance restores members from the same rows the catch-up re-reads, so
// its first LISTEN-landed signal must be a no-op — when the restore
// instead seeded presence as disconnected, every boot manufactured a
// memory-vs-rows mismatch and the "only when rows moved" guard projected
// a roomState that could land inside a resuming seat's hydration,
// between its snapshot and its chat replay (#1276 sighting #4, on
// #1293's CI run of StatsSurviveARestart). The seed must copy the row in
// both polarities — alice crashed connected, carol parked disconnected —
// so neither "seed false" nor "seed true" can hide in a one-sided room.
TEST_F(GamesHubStreamFixture, BootCatchUpProjectsNothingWhenRowsNeverMoved) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  auto carol = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(carol->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  // carol's clean close parks her seat and writes her row disconnected;
  // alice's park broadcast is the proof the flip landed before the
  // "crash". alice and bob crash with the process, rows still connected.
  carol->stream.Close();
  auto parked = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(parked.has_value());
  for (const auto& player : parked->as_roomState_or_null()->players) {
    if (player.playerId == carol->player_id) EXPECT_FALSE(player.connected);
  }
  store_->Flush();

  auto instance = BuildSecondInstance(vault_, store_, chat_store_);
  ASSERT_NE(instance, nullptr);
  auto resumed = OpenSeatVia(*instance->client, bob->resume_token);
  ASSERT_TRUE(resumed.has_value());

  // The documented hydration order, strictly — the frames the injected
  // projection was observed splitting.
  auto ready = NextEvent(resumed->stream);
  ASSERT_TRUE(ready.has_value());
  ASSERT_EQ(std::string(ready->case_name()), "sessionReady");
  auto snapshot = NextEvent(resumed->stream);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(std::string(snapshot->case_name()), "roomState");
  // The snapshot projects the restored seeds: row truth in both
  // directions, not a blanket value.
  const auto* lobby = snapshot->as_roomState_or_null();
  ASSERT_NE(lobby, nullptr);
  ASSERT_EQ(lobby->players.size(), 3u);
  for (const auto& player : lobby->players) {
    EXPECT_EQ(player.connected, player.playerId != carol->player_id)
        << "restored presence for " << player.playerId << " does not match its row";
  }
  auto replay = NextEvent(resumed->stream);
  ASSERT_TRUE(replay.has_value());
  ASSERT_EQ(std::string(replay->case_name()), "roomChatHistory");

  // The LISTEN-landed signals, called directly so no listener timing is
  // in play. Neither alice nor carol resumed here — their rows are what
  // a crashed instance leaves behind — yet nothing has moved since the
  // restore read these same rows, so both catch-ups must stay quiet.
  instance->golf->OnChannelActive(RoomChannel(room_id));
  instance->golf->OnChannelActive(ChatChannel(room_id));
  auto leftover = resumed->stream.Receive(std::chrono::milliseconds(50));
  EXPECT_TRUE(!leftover.ok() || !leftover->has_value())
      << "boot catch-up projected a frame though no row moved";
}

// The boot reaper works on wall time and rows, not frames, so its tests
// watch the store converge. Returns the last snapshot either way; the
// caller's assertions name what never arrived.
HubStore::Snapshot AwaitSnapshot(HubStore& store,
                                 const std::function<bool(const HubStore::Snapshot&)>& predicate,
                                 std::chrono::milliseconds budget = std::chrono::seconds(5)) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (true) {
    auto snapshot = store.LoadSnapshot();
    if (snapshot.ok() && predicate(*snapshot)) return *std::move(snapshot);
    if (std::chrono::steady_clock::now() >= deadline) {
      return snapshot.ok() ? *std::move(snapshot) : HubStore::Snapshot{};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
}

// The boot cohort's happy path (#1295): a restart converts a parked
// member's grace into forever-membership, because the registry that
// owned their timer died with the process. The successor's one boot
// deadline reaps what no session reclaims — alice, parked and never
// coming back — while bob (row connected) and carol (resumes here)
// survive. A smoke of the whole flow, not a unique pin of any one
// guard: the restore filter alone spares bob, and carol's Play erase
// and the drain's row check are indistinguishable from her survival.
// The per-guard pins live in BootGraceNeverClaimsASeatRestoredConnected
// (filter), BootGraceCedesASeatObservedConnectedMidWindow (cede), and
// BootGraceDrainSparesARowThatTurnedConnectedUnobserved (drain check).
TEST_F(GamesHubStreamFixture, BootGraceReapsParkedGhostAndSparesLiveSeats) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  auto carol = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(carol->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());

  // alice and carol park; their clean closes write disconnected through.
  alice->stream.Close();
  carol->stream.Close();
  auto parked = AwaitSnapshot(*store_, [](const HubStore::Snapshot& snapshot) {
    int disconnected = 0;
    for (const auto& member : snapshot.members) {
      if (!member.connected) ++disconnected;
    }
    return disconnected == 2;
  });
  ASSERT_EQ(parked.members.size(), 3u);

  // The "crash": a successor boots over the same rows while this
  // generation stays parked, saying no goodbyes. Two seconds of boot
  // grace — enough headroom for carol's loopback resume, short enough
  // for a test.
  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder(),
                                      /*grace=*/std::chrono::seconds(2));
  ASSERT_NE(instance, nullptr);
  auto carol_back = OpenSeatVia(*instance->client, carol->resume_token);
  ASSERT_TRUE(carol_back.has_value());
  EXPECT_EQ(carol_back->player_id, carol->player_id);
  ASSERT_TRUE(ReceiveCase(carol_back->stream, "sessionReady").has_value());

  auto reaped = AwaitSnapshot(
      *store_, [](const HubStore::Snapshot& snapshot) { return snapshot.members.size() == 2; },
      std::chrono::seconds(6));
  ASSERT_EQ(reaped.members.size(), 2u);
  std::set<std::string> survivors;
  for (const auto& member : reaped.members) survivors.insert(member.player_id);
  EXPECT_TRUE(survivors.contains(bob->player_id)) << "connected row was reaped";
  EXPECT_TRUE(survivors.contains(carol->player_id)) << "resumed seat was reaped";
}

// The muchq.github.io#260 symptom, hub half, both timelines. While a
// fossil membership is live, a resume lands back in it and a share-link
// join is refused with the exact #260 answer — that mid-window shape is
// the frontend's to fix (leave first), pinned here so the hub half is
// not mistaken for the whole fix. What the hub owes is the other
// timeline: the fossil must not answer that way *forever*. Once the
// boot grace clears it, a resume carries no room and the join lands.
TEST_F(GamesHubStreamFixture, ShareLinkJoinSucceedsOnceBootGraceClearsStaleMembership) {
  auto alice = OpenSeat();
  auto dave = OpenSeat();
  ASSERT_TRUE(alice.has_value() && dave.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(dave->stream, "sessionReady").has_value());
  const std::string old_room = CreateRoomFor(*alice);
  ASSERT_FALSE(old_room.empty());
  moonbase::games::JoinRoom join_old;
  join_old.roomId = old_room;
  ASSERT_TRUE(dave->stream.Send(GolfCommands::FromJoinroom(join_old)).ok());
  ASSERT_TRUE(ReceiveCase(dave->stream, "roomState").has_value());
  alice->stream.Close();
  dave->stream.Close();
  AwaitSnapshot(*store_, [](const HubStore::Snapshot& snapshot) {
    int disconnected = 0;
    for (const auto& member : snapshot.members) {
      if (!member.connected) ++disconnected;
    }
    return disconnected == 2;
  });

  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder(),
                                      /*grace=*/std::chrono::seconds(2));
  ASSERT_NE(instance, nullptr);
  // The share link's room, created on the new instance while the old
  // memberships age out.
  auto bob = OpenSeatVia(*instance->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string shared_room = created->as_roomState_or_null()->roomId;

  // dave follows the share link mid-window: his resume lands in the
  // fossil (which also spares him from the cohort), and the bare join
  // gets #260's exact answer. Production v2 always resumes first, so
  // this is the shape the frontend's leave-before-join exists for.
  auto dave_back = OpenSeatVia(*instance->client, dave->resume_token);
  ASSERT_TRUE(dave_back.has_value());
  auto dave_ready = ReceiveCase(dave_back->stream, "sessionReady");
  ASSERT_TRUE(dave_ready.has_value());
  ASSERT_TRUE(dave_ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*dave_ready->as_sessionReady_or_null()->roomId, old_room);
  moonbase::games::JoinRoom join_shared;
  join_shared.roomId = shared_room;
  ASSERT_TRUE(dave_back->stream.Send(GolfCommands::FromJoinroom(join_shared)).ok());
  auto refused = ReceiveCase(dave_back->stream, "commandRejected");
  ASSERT_TRUE(refused.has_value());
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "room unavailable or already in a room");

  // The reap clears alice's fossil; resumed dave keeps his seat, so the
  // old room survives him.
  auto cleared = AwaitSnapshot(
      *store_,
      [&](const HubStore::Snapshot& snapshot) {
        for (const auto& member : snapshot.members) {
          if (member.player_id == alice->player_id) return false;
        }
        return true;
      },
      std::chrono::seconds(6));
  std::set<std::string> remaining;
  for (const auto& member : cleared.members) remaining.insert(member.player_id);
  ASSERT_FALSE(remaining.contains(alice->player_id)) << "the boot deadline never reaped";
  ASSERT_TRUE(remaining.contains(dave->player_id));

  // The share-link visit on the hub's timeline: alice's resume finds no
  // fossil membership, and the join that #260 saw rejected goes through.
  auto alice_back = OpenSeatVia(*instance->client, alice->resume_token);
  ASSERT_TRUE(alice_back.has_value());
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(ready->as_sessionReady_or_null()->roomId.has_value());
  ASSERT_TRUE(alice_back->stream.Send(GolfCommands::FromJoinroom(join_shared)).ok());
  auto joined = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_roomState_or_null()->roomId, shared_room);
  EXPECT_EQ(joined->as_roomState_or_null()->players.size(), 2u);
}

// The fleet half of the boot cohort's contract (#1295): a seat restored
// with its row at connected belongs to whatever session owns that row —
// here, alice live on this generation — and must never enter the
// sibling's cohort at all. Otherwise her park during the sibling's boot
// window would be reaped at its boot deadline instead of running her
// own full grace on this generation's registry. bob, parked before the
// boot with his row at disconnected, is the canary that proves the
// deadline fired and the drain ran.
TEST_F(GamesHubStreamFixture, BootGraceNeverClaimsASeatRestoredConnected) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  bob->stream.Close();
  AwaitSnapshot(*store_, [&](const HubStore::Snapshot& snapshot) {
    for (const auto& member : snapshot.members) {
      if (member.player_id == bob->player_id) return !member.connected;
    }
    return false;
  });

  // The sibling boots while alice is live: her row reads connected.
  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder(),
                                      /*grace=*/std::chrono::seconds(1));
  ASSERT_NE(instance, nullptr);
  // She parks inside the sibling's boot window; this generation's
  // registry arms her grace (60s here — far past the test). Await the
  // row flip before watching for the reap: a drain that read her row
  // still connected would spare her for the wrong reason and mask a
  // deleted restore filter.
  alice->stream.Close();
  AwaitSnapshot(*store_, [&](const HubStore::Snapshot& snapshot) {
    for (const auto& member : snapshot.members) {
      if (member.player_id == alice->player_id) return !member.connected;
    }
    return false;
  });

  // bob's reap is the deadline's proof; alice must outlive it.
  auto reaped = AwaitSnapshot(
      *store_,
      [&](const HubStore::Snapshot& snapshot) {
        for (const auto& member : snapshot.members) {
          if (member.player_id == bob->player_id) return false;
        }
        return true;
      },
      std::chrono::seconds(6));
  ASSERT_EQ(reaped.members.size(), 1u);
  EXPECT_EQ(reaped.members[0].player_id, alice->player_id)
      << "a seat restored connected was claimed by the sibling's boot cohort";
}

// The same contract's second window: a seat that entered the cohort
// parked, but whose row a reconcile later observed at connected —
// bob resumes on this generation mid-window — is ceded to that row's
// owner for good. His second park then runs this generation's full
// grace, not the remainder of the sibling's boot deadline. carol is
// the parked-ghost canary again.
TEST_F(GamesHubStreamFixture, BootGraceCedesASeatObservedConnectedMidWindow) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  auto carol = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(carol->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  bob->stream.Close();
  carol->stream.Close();
  AwaitSnapshot(*store_, [](const HubStore::Snapshot& snapshot) {
    int disconnected = 0;
    for (const auto& member : snapshot.members) {
      if (!member.connected) ++disconnected;
    }
    return disconnected == 2;
  });

  // The sibling boots with bob and carol both parked — both cohort.
  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder(),
                                      /*grace=*/std::chrono::seconds(2));
  ASSERT_NE(instance, nullptr);

  // bob resumes on this generation: his row flips to connected, and the
  // sibling's LISTEN-landed reconcile observes it (direct call, no
  // listener timing) — the cede under test.
  auto bob_back = OpenSeat(bob->resume_token);
  ASSERT_TRUE(bob_back.has_value());
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());
  instance->golf->OnChannelActive(RoomChannel(room_id));
  // He parks again, still inside the sibling's window: this
  // generation's registry owns his fresh grace now.
  bob_back->stream.Close();
  AwaitSnapshot(*store_, [&](const HubStore::Snapshot& snapshot) {
    for (const auto& member : snapshot.members) {
      if (member.player_id == bob->player_id) return !member.connected;
    }
    return false;
  });

  // carol's reap proves the deadline fired; ceded bob must outlive it.
  auto reaped = AwaitSnapshot(
      *store_,
      [&](const HubStore::Snapshot& snapshot) {
        for (const auto& member : snapshot.members) {
          if (member.player_id == carol->player_id) return false;
        }
        return true;
      },
      std::chrono::seconds(6));
  std::set<std::string> survivors;
  for (const auto& member : reaped.members) survivors.insert(member.player_id);
  // Hard canary: AwaitSnapshot hands back its last look on timeout, so
  // a reaper that never ran would leave carol present and every spare
  // assertion below vacuously green.
  ASSERT_FALSE(survivors.contains(carol->player_id)) << "the boot deadline never reaped";
  EXPECT_TRUE(survivors.contains(alice->player_id));
  EXPECT_TRUE(survivors.contains(bob->player_id))
      << "a seat whose row a reconcile observed connected was still reaped by the boot cohort";
}

// The drain's own row check, pinned in the one shape neither the
// restore filter nor the cede can reach: bob enters the cohort parked,
// turns connected by resuming on this generation, and no reconcile ever
// shows the sibling that pulse — so only the drain-time fresh read
// stands between him and a wrongful reap. carol is the parked-ghost
// canary proving the deadline fired.
TEST_F(GamesHubStreamFixture, BootGraceDrainSparesARowThatTurnedConnectedUnobserved) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  auto carol = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value() && carol.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(carol->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(carol->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(carol->stream, "roomState").has_value());
  bob->stream.Close();
  carol->stream.Close();
  AwaitSnapshot(*store_, [](const HubStore::Snapshot& snapshot) {
    int disconnected = 0;
    for (const auto& member : snapshot.members) {
      if (!member.connected) ++disconnected;
    }
    return disconnected == 2;
  });

  // The sibling boots with bob and carol both parked — both cohort.
  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder(),
                                      /*grace=*/std::chrono::seconds(2));
  ASSERT_NE(instance, nullptr);
  // bob resumes on this generation. Deliberately no OnChannelActive on
  // the sibling: it never observes the connected pulse, so bob stays in
  // its cohort and only the drain's fresh read can spare him.
  auto bob_back = OpenSeat(bob->resume_token);
  ASSERT_TRUE(bob_back.has_value());
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());

  auto reaped = AwaitSnapshot(
      *store_,
      [&](const HubStore::Snapshot& snapshot) {
        for (const auto& member : snapshot.members) {
          if (member.player_id == carol->player_id) return false;
        }
        return true;
      },
      std::chrono::seconds(6));
  std::set<std::string> survivors;
  for (const auto& member : reaped.members) survivors.insert(member.player_id);
  ASSERT_FALSE(survivors.contains(carol->player_id)) << "the boot deadline never reaped";
  EXPECT_TRUE(survivors.contains(alice->player_id));
  EXPECT_TRUE(survivors.contains(bob->player_id))
      << "the drain reaped a row its own fresh read shows connected";
}

// The once-guard is behavior, not just a comment: a second restore
// would stack a fresh cohort behind a reaper that never re-arms —
// forever-membership again, silently. Refusal must not depend on
// whether the first restore happened to arm a thread.
TEST_F(GamesHubStreamFixture, RestoreFromStoreRefusesASecondCall) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_FALSE(CreateRoomFor(*alice).empty());
  store_->Flush();

  auto instance = BuildSecondInstance(vault_, store_, chat_store_, MakeCapturingMetricsRecorder());
  ASSERT_NE(instance, nullptr);
  const absl::Status again = instance->golf->RestoreFromStore();
  EXPECT_EQ(again.code(), absl::StatusCode::kFailedPrecondition) << again;

  // The fixture's own hub restored an empty store — no thread armed —
  // and must refuse a second call all the same.
  const absl::Status empty_again = golf_->RestoreFromStore();
  EXPECT_EQ(empty_again.code(), absl::StatusCode::kFailedPrecondition) << empty_again;
}

TEST_F(GamesHubStreamFixture, JoinHistoryIsCappedAtTheRetentionLimit) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  // One over the retention window, so the replay must both cap at the
  // limit and hold the newest end — a hardcoded smaller LoadRecent limit
  // or an off-by-one prune fails here, where the 3-message test cannot.
  for (std::size_t i = 1; i <= kChatHistoryLimit + 1; ++i) {
    moonbase::games::Chat chat;
    chat.text = "m-" + std::to_string(i);
    ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
    ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  }

  auto bob = OpenSeat();
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join;
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

TEST_F(GamesHubStreamFixture, AFailedHistoryLoadDoesNotFailTheResume) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  moonbase::games::Chat chat;
  chat.text = "stored fine";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());

  // History is best-effort: when the load fails, the resume still lands
  // (sessionReady, roomState) and the stream simply hears no history
  // event — the model's documented absence case.
  auto capture = MakeCapturingMetricsRecorder();
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
TEST_F(GamesHubStreamFixture, ChatMetricsCountOutcomesWithoutIdentifiers) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  moonbase::games::Chat chat;
  chat.text = "counted, never labeled";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomChat").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChat").has_value());

  // One stored append, one drain that delivered its one row, one
  // history replay (bob's join; createRoom sends none).
  EXPECT_EQ(metrics_->CounterTotal("chat_appends", {{"result", "stored"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered"), 1);
  EXPECT_EQ(metrics_->CounterTotal("chat_catch_up_drains"), 1);
  EXPECT_EQ(metrics_->CounterTotal("chat_history_replays"), 1);

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

// The zero baseline (#1323): a handler must put its counters on the wire at 0
// when it is built, before any session. Without it each series is born carrying
// its first event's value and increase() shows nothing for that event, ever —
// so the first chat message, the first refused admission, the first reaped seat
// after a deploy are all uncounted.
//
// Every declared series is asserted individually, attributes included, because
// a series is name *and* attributes: a bare chat_appends baselines an orphan
// while chat_appends{result="stored"} — the one the messages tile queries — stays
// unbaselined.
//
// The expected roster is written out here rather than read off the handler, and
// the duplication is the point. Reading DeclaredCounterSeries() would make the
// loop iterate the same list production iterates, so deleting an entry would
// delete its assertion too and the test would stay green while a dashboard went
// blind. This copy is what makes a deletion cost two edits instead of one — and
// it is the only guard against the reverse mistake, a series declared here that
// no emit site ever writes, which nothing else in the suite can see.
TEST_F(GamesHubStreamFixture, BuildingAHandlerDeclaresEveryCounterSeriesAtZero) {
  const std::vector<GolfHub::CounterSeries> kExpected = {
      {"castle_commands", {{"command", "createGame"}}},
      {"castle_commands", {{"command", "joinGame"}}},
      {"castle_commands", {{"command", "startGame"}}},
      {"castle_commands", {{"command", "leaveGame"}}},
      {"castle_commands", {{"command", "swapForSetup"}}},
      {"castle_commands", {{"command", "ready"}}},
      {"castle_commands", {{"command", "playFromHand"}}},
      {"castle_commands", {{"command", "playFaceUp"}}},
      {"castle_commands", {{"command", "playFaceDown"}}},
      {"castle_commands", {{"command", "pickUp"}}},
      {"castle_events", {{"event", "gameJoined"}}},
      {"castle_events", {{"event", "gameState"}}},
      {"castle_events", {{"event", "gameCreated"}}},
      {"castle_events", {{"event", "gameStarted"}}},
      {"castle_events", {{"event", "turnChanged"}}},
      {"castle_events", {{"event", "gameEnded"}}},
      {"castle_events", {{"event", "gameLeft"}}},
      {"chat_appends", {{"result", "stored"}}},
      {"chat_appends", {{"result", "rejected"}}},
      {"chat_appends", {{"result", "unavailable"}}},
      {"chat_catch_up_drains", {}},
      {"chat_failures", {{"stage", "cursor_seed"}}},
      {"chat_failures", {{"stage", "catch_up"}}},
      {"chat_failures", {{"stage", "history_load"}}},
      {"chat_history_replays", {}},
      {"chat_rows_delivered", {}},
      {"golf_admissions_refused", {{"reason", "bad_ticket"}}},
      {"golf_admissions_refused", {{"reason", "seat_conflict"}}},
      {"golf_commands", {{"command", "createRoom"}}},
      {"golf_commands", {{"command", "joinRoom"}}},
      {"golf_commands", {{"command", "leaveRoom"}}},
      {"golf_commands", {{"command", "getRoomState"}}},
      {"golf_commands", {{"command", "chat"}}},
      {"golf_commands", {{"command", "golf.createGame"}}},
      {"golf_commands", {{"command", "golf.joinGame"}}},
      {"golf_commands", {{"command", "golf.startGame"}}},
      {"golf_commands", {{"command", "golf.leaveGame"}}},
      {"golf_commands", {{"command", "golf.peekCard"}}},
      {"golf_commands", {{"command", "golf.drawCard"}}},
      {"golf_commands", {{"command", "golf.takeFromDiscard"}}},
      {"golf_commands", {{"command", "golf.swapCard"}}},
      {"golf_commands", {{"command", "golf.discardDrawn"}}},
      {"golf_commands", {{"command", "golf.knock"}}},
      {"golf_commands", {{"command", "golf.hideCards"}}},
      {"golf_disconnects", {{"kind", "clean"}}},
      {"golf_disconnects", {{"kind", "abrupt"}}},
      {"golf_events", {{"event", "sessionReady"}}},
      {"golf_events", {{"event", "roomState"}}},
      {"golf_events", {{"event", "roomLeft"}}},
      {"golf_events", {{"event", "roomChat"}}},
      {"golf_events", {{"event", "roomChatHistory"}}},
      {"golf_events", {{"event", "commandRejected"}}},
      {"golf_events", {{"event", "golf.gameJoined"}}},
      {"golf_events", {{"event", "golf.gameState"}}},
      {"golf_events", {{"event", "golf.gameCreated"}}},
      {"golf_events", {{"event", "golf.gameStarted"}}},
      {"golf_events", {{"event", "golf.turnChanged"}}},
      {"golf_events", {{"event", "golf.playerKnocked"}}},
      {"golf_events", {{"event", "golf.gameEnded"}}},
      {"golf_events", {{"event", "golf.gameLeft"}}},
      {"golf_rate_limited", {{"kind", "chat"}}},
      {"golf_rate_limited", {{"kind", "command"}}},
      {"golf_rejections", {{"kind", "rate_limited"}}},
      {"golf_rejections", {{"kind", "invalid"}}},
      {"golf_rejections", {{"kind", "state"}}},
      {"golf_rejections", {{"kind", "rules"}}},
      {"golf_rejections", {{"kind", "unavailable"}}},
      {"golf_rejections", {{"kind", "unknown"}}},
      {"golf_restored_seats_reaped", {}},
      {"golf_seats_expired", {}},
      {"golf_sessions", {{"resumed", "true"}}},
      {"golf_sessions", {{"resumed", "false"}}},
      // The thoughts hub's list (#79), declared by the ThoughtsHub built on
      // the same recorder.
      {"thoughts_admissions_refused", {{"reason", "bad_ticket"}}},
      {"thoughts_admissions_refused", {{"reason", "seat_conflict"}}},
      {"thoughts_commands", {{"command", "join"}}},
      {"thoughts_commands", {{"command", "move"}}},
      {"thoughts_commands", {{"command", "shape"}}},
      {"thoughts_commands", {{"command", "leave"}}},
      {"thoughts_disconnects", {{"kind", "clean"}}},
      {"thoughts_disconnects", {{"kind", "abrupt"}}},
      {"thoughts_events", {{"event", "sessionReady"}}},
      {"thoughts_events", {{"event", "worldState"}}},
      {"thoughts_events", {{"event", "playerJoined"}}},
      {"thoughts_events", {{"event", "playerMoved"}}},
      {"thoughts_events", {{"event", "shapeChanged"}}},
      {"thoughts_events", {{"event", "playerLeft"}}},
      {"thoughts_events", {{"event", "commandRejected"}}},
      {"thoughts_rate_limited", {}},
      {"thoughts_rejections", {{"kind", "rate_limited"}}},
      {"thoughts_rejections", {{"kind", "invalid"}}},
      {"thoughts_rejections", {{"kind", "state"}}},
      {"thoughts_rejections", {{"kind", "unknown"}}},
      {"thoughts_sessions", {}},
  };

  auto capture = MakeCapturingMetricsRecorder();
  auto instance = BuildSecondInstance(vault_, store_, chat_store_, capture);
  ASSERT_NE(instance, nullptr);

  // Count first, so dropping an entry from the handler's list fails here rather
  // than only failing whichever per-series assertion happened to cover it.
  EXPECT_EQ(GamesHubHandler::DeclaredCounterSeries().size(), kExpected.size());

  const auto entries = capture->Entries();
  for (const auto& series : kExpected) {
    const std::string label = SeriesLabel(series);
    EXPECT_EQ(capture->CounterTotal(series.name, series.attributes), 0) << label;
    // Presence, not just a zero read: CounterTotal sums an empty match to 0, so
    // the assertion above passes just as happily against a series that was
    // never declared — including one whose name a rename left behind.
    const bool declared = std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
      return entry.name == series.name && entry.attributes == series.attributes;
    });
    EXPECT_TRUE(declared) << label << " was never put on the wire";
  }
}

// The other direction — a series emitted but not declared, which loses its first
// occurrence — is not pinned here. It is pinned in the fixture's TearDown by
// ExpectOnlyDeclaredCounterSeries, so every test in this suite checks it against
// whatever paths that test happens to drive: the bad-ticket admission, the rate
// limiter, the chat store failures, and the disconnects the closes produce. One
// scripted session would have covered a small fraction of the roster.

// The golf_commands/golf_events declarations are a hand copy of the model's
// union cases, and this test is what makes a hand copy safe to keep: it reads
// golf.smithy and fails on drift in either direction — a case added to the
// model without a declaration loses its first event after every deploy (#1323),
// and a declaration for a case the model no longer has is a permanently flat
// line on a dashboard. CountCommand and Send name series exactly this way:
// the outer case name, or golf.<inner case> through the envelope. The castle
// envelope counts on its own castle_* series (the pin below), so its outer
// case is not a golf_ label.
TEST(StreamSeriesModelPin, StreamSeriesMatchTheModelUnions) {
  const std::string model = ReadModel("domains/games/apis/games_hub/model/golf.smithy");
  ASSERT_FALSE(model.empty());

  const auto outer_commands = ModelUnionCases(model, "GolfCommands");
  const auto moves = ModelUnionCases(model, "GolfMove");
  const auto outer_events = ModelUnionCases(model, "GolfEvents");
  const auto updates = ModelUnionCases(model, "GolfUpdate");
  ASSERT_NE(std::find(outer_commands.begin(), outer_commands.end(), "castle"),
            outer_commands.end());

  // Controls before the comparison: a parser that quietly matched nothing
  // must fail here, not produce two empty sets that agree.
  ASSERT_NE(std::find(outer_commands.begin(), outer_commands.end(), "createRoom"),
            outer_commands.end());
  ASSERT_NE(std::find(moves.begin(), moves.end(), "knock"), moves.end());
  ASSERT_NE(std::find(outer_events.begin(), outer_events.end(), "sessionReady"),
            outer_events.end());
  ASSERT_NE(std::find(updates.begin(), updates.end(), "gameEnded"), updates.end());

  std::set<std::string> expected_commands;
  for (const auto& name : outer_commands) {
    if (name != "golf" && name != "castle") expected_commands.insert(name);
  }
  for (const auto& name : moves) expected_commands.insert("golf." + name);

  std::set<std::string> expected_events;
  for (const auto& name : outer_events) {
    if (name != "golf" && name != "castle") expected_events.insert(name);
  }
  for (const auto& name : updates) expected_events.insert("golf." + name);

  EXPECT_EQ(DeclaredLabelValues("golf_commands", "command"), expected_commands);
  EXPECT_EQ(DeclaredLabelValues("golf_events", "event"), expected_events);
}

// Castle's envelope (#77) counts its inner case names on castle_commands and
// castle_events, pinned against castle.smithy the same way.
TEST(StreamSeriesModelPin, CastleSeriesMatchTheModelUnions) {
  const std::string model = ReadModel("domains/games/apis/games_hub/model/castle.smithy");
  ASSERT_FALSE(model.empty());
  const auto moves = ModelUnionCases(model, "CastleMove");
  const auto updates = ModelUnionCases(model, "CastleUpdate");
  ASSERT_NE(std::find(moves.begin(), moves.end(), "pickUp"), moves.end());
  ASSERT_NE(std::find(updates.begin(), updates.end(), "gameEnded"), updates.end());
  EXPECT_EQ(DeclaredLabelValues("castle_commands", "command"),
            std::set<std::string>(moves.begin(), moves.end()));
  EXPECT_EQ(DeclaredLabelValues("castle_events", "event"),
            std::set<std::string>(updates.begin(), updates.end()));
}

// The bounded kind is the whole dashboard identity of a rejection (#1327,
// #1384): each refusal lands on exactly one declared kind series, and the
// free-text reason reaches the rejected player but never a metric label —
// those strings are unbounded, and one in a label reopens the undeclarable
// hole the kind exists to close. TearDown's sweep enforces the closed set for
// every test; this one pins which kind each refusal shape maps to.
TEST_F(GamesHubStreamFixture, RejectionsCountTheBoundedKindNeverTheReason) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // state: well-formed, but alice is already in a room.
  ASSERT_TRUE(
      table->alice.stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto state_rejected = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(state_rejected.has_value());
  const std::string state_reason = state_rejected->as_commandRejected_or_null()->reason;
  EXPECT_EQ(state_reason, "already in a room");

  // invalid: a card index no hand has — through peekCard, and through
  // takeFromDiscard, which no other test in the tree sends at all, so its
  // declared golf_commands series meets an emit here.
  moonbase::games::PeekCard peek;
  peek.cardIndex = 99;
  ASSERT_TRUE(table->alice.stream.Send(Move(moonbase::games::GolfMove::FromPeekcard(peek))).ok());
  auto invalid_rejected = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(invalid_rejected.has_value());
  const std::string invalid_reason = invalid_rejected->as_commandRejected_or_null()->reason;
  moonbase::games::TakeFromDiscard take;
  take.cardIndex = 99;
  ASSERT_TRUE(
      table->alice.stream.Send(Move(moonbase::games::GolfMove::FromTakefromdiscard(take))).ok());
  ASSERT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  // rules: a valid index, but the engine refuses a swap with no drawn card.
  moonbase::games::SwapCard swap;
  swap.cardIndex = 0;
  ASSERT_TRUE(table->alice.stream.Send(Move(moonbase::games::GolfMove::FromSwapcard(swap))).ok());
  auto rules_rejected = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(rules_rejected.has_value());
  const std::string rules_reason = rules_rejected->as_commandRejected_or_null()->reason;

  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "invalid"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "rules"}}), 1);
  // The kinds nothing here triggered, so a mapping change cannot hide as a
  // different kind absorbing the count.
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "rate_limited"}}), 0);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "unavailable"}}), 0);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "unknown"}}), 0);

  // The reason text stayed off the wire's metrics entirely: every rejection
  // entry carries the kind label alone, and no label anywhere carries any of
  // the human-readable reasons the client was sent.
  for (const auto& entry : metrics_->Entries()) {
    if (entry.name == "golf_rejections") {
      ASSERT_EQ(entry.attributes.size(), 1u);
      ASSERT_EQ(entry.attributes.count("kind"), 1u);
    }
    for (const auto& [key, value] : entry.attributes) {
      for (const std::string& reason : {state_reason, invalid_reason, rules_reason}) {
        EXPECT_EQ(value.find(reason), std::string::npos) << entry.name << " " << key;
      }
    }
  }
}

// Forwards everything except LoadRoom, which always fails as if the database
// were unreachable — the join paths' refresh read, which MemoryHubStore can
// never fail on its own.
class FailingLoadRoomHubStore final : public HubStore {
 public:
  explicit FailingLoadRoomHubStore(std::shared_ptr<HubStore> delegate)
      : delegate_(std::move(delegate)) {}

  void Enqueue(std::vector<Op> ops) override { delegate_->Enqueue(std::move(ops)); }
  void Flush() override { delegate_->Flush(); }
  absl::StatusOr<Snapshot> LoadSnapshot() override { return delegate_->LoadSnapshot(); }
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override {
    return delegate_->CommitGameSave(row, notify_payload);
  }
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& deltas,
                                        const std::string& notify_payload) override {
    return delegate_->CommitGameFinish(row, deltas, notify_payload);
  }
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override {
    return delegate_->LoadGame(room_id, game_id);
  }
  absl::StatusOr<RoomRows> LoadRoom([[maybe_unused]] const std::string& room_id) override {
    return absl::UnavailableError("hub store unreachable");
  }

 private:
  std::shared_ptr<HubStore> delegate_;
};

// The store answering "no such room" and the store not answering are
// different rejection kinds: the first is the client's state, the second is
// this hub's outage, and folding them (the pre-#1384 shape) makes a Postgres
// outage read as a spike of desynced clients. The healthy-store halves are
// the positive controls proving the kUnavailable reads come from the outage
// and not from the join shapes themselves.
TEST_F(GamesHubStreamFixture, AStoreOutageOnTheJoinPathsCountsAsUnavailable) {
  // Control: the same two refusals against the fixture's healthy store.
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  moonbase::games::JoinRoom join;
  join.roomId = "no-such-room";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "commandRejected").has_value());
  ASSERT_FALSE(CreateRoomFor(*alice).empty());
  moonbase::games::JoinGame join_game;
  join_game.gameId = "no-such-game";
  ASSERT_TRUE(alice->stream.Send(Move(moonbase::games::GolfMove::FromJoingame(join_game))).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "commandRejected").has_value());
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "state"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "unavailable"}}), 0);

  // The outage: the same two shapes through a store whose refresh read fails.
  auto capture = MakeCapturingMetricsRecorder();
  auto instance = BuildSecondInstance(vault_, std::make_shared<FailingLoadRoomHubStore>(store_),
                                      chat_store_, capture);
  ASSERT_NE(instance, nullptr);
  auto bob = OpenSeatVia(*instance->client);
  ASSERT_TRUE(bob.has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto room_refused = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(room_refused.has_value());
  EXPECT_EQ(room_refused->as_commandRejected_or_null()->reason,
            "room unavailable or already in a room");
  ASSERT_FALSE(CreateRoomFor(*bob).empty());
  ASSERT_TRUE(bob->stream.Send(Move(moonbase::games::GolfMove::FromJoingame(join_game))).ok());
  auto game_refused = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(game_refused.has_value());
  EXPECT_EQ(game_refused->as_commandRejected_or_null()->reason, "storage unavailable; try again");
  EXPECT_EQ(capture->CounterTotal("golf_rejections", {{"kind", "unavailable"}}), 2);
  EXPECT_EQ(capture->CounterTotal("golf_rejections", {{"kind", "state"}}), 0);
}

TEST_F(GamesHubStreamFixture, AnUnreachableStoreCountsTheAppendAsUnavailable) {
  auto capture = MakeCapturingMetricsRecorder();
  auto instance = BuildSecondInstance(
      vault_, store_, std::make_shared<FailingAppendChatStore>(chat_store_), capture);
  ASSERT_NE(instance, nullptr);
  auto alice = OpenSeatVia(*instance->client);
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::games::Chat chat;
  chat.text = "never stored";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto rejected = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "chat is unavailable");

  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "unavailable"}}), 1);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "stored"}}), 0);
  EXPECT_EQ(capture->CounterTotal("chat_rows_delivered"), 0);
  EXPECT_EQ(capture->CounterTotal("golf_rejections", {{"kind", "unavailable"}}), 1);
}

// The rejected side of the same counter: the store says the sender's
// membership vanished mid-send (the race store-level authorization
// exists for), the client hears the same "not in a room" a pre-store
// reject uses, and the outcome counts as rejected — an authorization
// answer, not an outage.
TEST_F(GamesHubStreamFixture, AStaleMembershipCountsTheAppendAsRejected) {
  auto capture = MakeCapturingMetricsRecorder();
  auto instance = BuildSecondInstance(vault_, store_,
                                      std::make_shared<NotAMemberChatStore>(chat_store_), capture);
  ASSERT_NE(instance, nullptr);
  auto alice = OpenSeatVia(*instance->client);
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::games::Chat chat;
  chat.text = "membership just vanished";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto rejected = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "rejected"}}), 1);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "unavailable"}}), 0);
  EXPECT_EQ(capture->CounterTotal("chat_appends", {{"result", "stored"}}), 0);
  EXPECT_EQ(capture->CounterTotal("golf_rejections", {{"kind", "state"}}), 1);
}

// Drain metrics at batch grain: one wake that finds two committed rows
// delivers them as one drain — rows grow by the batch while drains grow by
// one, which is what makes rate(rows)/rate(drains) read as "rows behind at
// the wake". A redundant wake then counts the drain that delivered nothing,
// which is what keeps that windowed average honest.
TEST_F(GamesHubStreamFixture, DrainMetricsCountBatchesAndZeroWakes) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());
  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomChatHistory").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  // Two rows land behind the hub's back — committed straight through the
  // store, the shape of another instance's appends.
  ASSERT_TRUE(chat_store_->Append(room_id, alice->player_id, "first behind", "remote").ok());
  ASSERT_TRUE(chat_store_->Append(room_id, alice->player_id, "second behind", "remote").ok());

  const double delivered_before = metrics_->CounterTotal("chat_rows_delivered");
  const double drains_before = metrics_->CounterTotal("chat_catch_up_drains");

  golf_->OnNotify(ChatChannel(room_id), "remote-instance");
  for (auto* seat : {&*alice, &*bob}) {
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomChat").has_value());
    ASSERT_TRUE(ReceiveCase(seat->stream, "roomChat").has_value());
  }
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered") - delivered_before, 2);
  EXPECT_EQ(metrics_->CounterTotal("chat_catch_up_drains") - drains_before, 1);

  // Redundant wake: one more drain, nothing counted as delivered — the
  // batch-of-zero the windowed average needs. OnNotify pumps synchronously,
  // so no waiting.
  golf_->OnNotify(ChatChannel(room_id), "remote-instance");
  EXPECT_EQ(metrics_->CounterTotal("chat_rows_delivered") - delivered_before, 2);
  EXPECT_EQ(metrics_->CounterTotal("chat_catch_up_drains") - drains_before, 2);
}

// The membership guard, exercised through the handler that owns it
// rather than a test double. MemoryChatStore authorizes every append
// through this, so what it answers is what decides whether a message
// can be stored.
TEST_F(GamesHubStreamFixture, WithMemberRunsOnlyForCurrentMembers) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;

  moonbase::games::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  bool ran = false;
  EXPECT_TRUE(golf_->WithMember(room_id, alice->player_id, [&] { ran = true; }));
  EXPECT_TRUE(ran);

  ran = false;
  EXPECT_FALSE(golf_->WithMember(room_id, "nobody", [&] { ran = true; }));
  EXPECT_FALSE(golf_->WithMember("no-such-room", alice->player_id, [&] { ran = true; }));
  EXPECT_FALSE(ran) << "the action must not run when the seat is not there";

  // Leaving revokes it, which is what keeps a chat append off a seat
  // that is already gone.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveroom(moonbase::games::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomLeft").has_value());
  EXPECT_FALSE(golf_->WithMember(room_id, bob->player_id, [&] { ran = true; }));
  EXPECT_FALSE(ran);

  // And a message from the revoked seat is refused rather than echoed.
  moonbase::games::Chat chat;
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
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
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
  moonbase::games::PeekCard peek;
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

  // The window runs out with bob still gone. The ceremony's terminal
  // view precedes the result — and a forfeit is not a knock, so it
  // names no knocker.
  std::optional<moonbase::games::GameView> ended_view;
  for (int i = 0; i < 4 && !ended_view.has_value(); ++i) {
    auto view_update = ReceiveGolf(alice.stream, "gameState");
    ASSERT_TRUE(view_update.has_value());
    const auto& view = view_update->as_gameState_or_null()->view;
    if (view.phase == "ended") ended_view = view;
  }
  ASSERT_TRUE(ended_view.has_value());
  EXPECT_FALSE(ended_view->knockedPlayerId.has_value());

  // Alice takes the game, and the summary keeps bob's seat — name and
  // standing score — next to hers.
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
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  auto rejected = ReceiveCase(table->bob.stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not your turn");

  // A bad index dies before it reaches the engine.
  moonbase::games::PeekCard peek;
  peek.cardIndex = 9;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto bad_index = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(bad_index.has_value());
  EXPECT_EQ(bad_index->as_commandRejected_or_null()->reason, "invalid card index");

  // No blind moves: swapping without drawing is refused in-band.
  moonbase::games::SwapCard blind;
  blind.cardIndex = 0;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromSwapcard(blind))).ok());
  auto blind_swap = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(blind_swap.has_value());
  EXPECT_EQ(blind_swap->as_commandRejected_or_null()->reason, "no drawn card to swap");

  // The stream survived: a legal move still lands.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok());
  EXPECT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
}

TEST_F(GolfGameFixture, PendingGameLifecycleAndLobbySummaries) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  // The whole room hears the creation attributed to its creator; the
  // creator's client relies on createdBy to recognize its own echo.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
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

  moonbase::games::JoinGame nul_join;
  nul_join.gameId = WithNul(joined->as_gameJoined_or_null()->view.gameId, "alias");
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromJoingame(nul_join))).ok());
  auto invalid_game = ReceiveCase(bob->stream, "commandRejected");
  ASSERT_TRUE(invalid_game.has_value());
  EXPECT_EQ(invalid_game->as_commandRejected_or_null()->reason, "invalid game id");

  // A solo game cannot start.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::games::StartGame{}))).ok());
  auto lonely = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(lonely.has_value());
  EXPECT_EQ(lonely->as_commandRejected_or_null()->reason, "need at least 2 players to start");

  // One game per player at a time.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto second = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_commandRejected_or_null()->reason, "leave your current game first");

  // The lobby sees the pending game: waiting, one seat filled.
  ASSERT_TRUE(
      bob->stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto lobby = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  {
    const auto* room = lobby->as_roomState_or_null();
    ASSERT_EQ(room->games.size(), 1u);
    EXPECT_EQ(room->games[0].status, "waiting");
    EXPECT_EQ(room->games[0].playerCount, 1);
  }

  // Leaving a pending game as its last member dissolves it.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromLeavegame(moonbase::games::LeaveGame{}))).ok());
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
    if (!alice.stream.Send(Move(GolfMove::FromKnock(moonbase::games::Knock{}))).ok()) return false;
    if (!ReceiveGolf(bob.stream, "playerKnocked").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::games::DrawCard{}))).ok()) {
      return false;
    }
    if (!ReceiveGolf(bob.stream, "gameState").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::games::DiscardDrawn{}))).ok()) {
      return false;
    }
    return ReceiveGolf(alice.stream, "gameEnded").has_value() &&
           ReceiveGolf(bob.stream, "gameEnded").has_value();
  };
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Round two in the same room.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto joined = ReceiveGolf(table->alice.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  moonbase::games::JoinGame join;
  join.gameId = joined->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(table->bob.stream.Send(Move(GolfMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameJoined").has_value());
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromStartgame(moonbase::games::StartGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameStarted").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameStarted").has_value());
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Running totals: two games played, both won solo by alice the knocker
  // (identical zero-scoring deals; the knocker takes the tie).
  ASSERT_TRUE(
      table->alice.stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{}))
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
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::games::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
  auto first = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->as_gameJoined_or_null()->view.gameId, "DUPLIC");

  // Bob's create draws "DUPLIC" again; the hub rolls until it's fresh.
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromCreategame(moonbase::games::CreateGame{}))).ok());
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
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
  auto first = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->as_roomState_or_null()->roomId, "SAMERM");

  // Bob's create draws "SAMERM" again; the hub rolls until it's fresh.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromCreateroom(moonbase::games::CreateRoom{})).ok());
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

class FailingVaultFixture : public GamesHubStreamFixture {
 protected:
  std::shared_ptr<TicketVault> MakeVault() override { return std::make_shared<FailingVault>(); }
};

TEST_F(FailingVaultFixture, GetSessionFailsClosedWhenTheVaultIsDown) {
  const auto session = client_->GetSession(moonbase::games::GetSessionInput{});
  ASSERT_FALSE(session.ok());
}

// Per-session stream budgets (#1240), pinned with tiny buckets whose
// refills are effectively never — nothing a test does can accidentally
// earn a token back, so the refusal path is deterministic.
class RateLimitedStreamFixture : public GamesHubStreamFixture {
 protected:
  RateLimits MakeRateLimits() override {
    RateLimits limits;
    limits.command_burst = 6;
    limits.command_refill_per_sec = 0.0001;
    limits.chat_burst = 2;
    limits.chat_refill_per_sec = 0.0001;
    return limits;
  }
};

// The chat sub-limit: the burst stores and echoes, the message after it
// is refused before any locked work — told "slow down", counted by
// kind, and never stored. The session stays usable.
TEST_F(RateLimitedStreamFixture, AChatFloodStoresTheBurstAndRejectsTheRest) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  const std::string room_id = CreateRoomFor(*alice);
  ASSERT_FALSE(room_id.empty());

  moonbase::games::Chat chat;
  for (const char* text : {"one", "two"}) {
    chat.text = text;
    ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
    auto echo = NextEvent(alice->stream);
    ASSERT_TRUE(echo.has_value());
    ASSERT_NE(echo->as_roomChat_or_null(), nullptr);
    EXPECT_EQ(echo->as_roomChat_or_null()->text, text);
  }

  chat.text = "three is a flood";
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromChat(chat)).ok());
  auto refused = NextEvent(alice->stream);
  ASSERT_TRUE(refused.has_value());
  ASSERT_NE(refused->as_commandRejected_or_null(), nullptr);
  EXPECT_EQ(refused->as_commandRejected_or_null()->reason, "slow down");

  EXPECT_EQ(metrics_->CounterTotal("chat_appends", {{"result", "stored"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("golf_rate_limited", {{"kind", "chat"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_rate_limited", {{"kind", "command"}}), 0);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "rate_limited"}}), 1);
}

// The command budget guards everything, including frames that only ever
// earn rejections — the flood costs the hub almost nothing and honest
// commands resume once the client backs off (here: never, the refill is
// frozen, which is what makes the boundary exact).
TEST_F(RateLimitedStreamFixture, ACommandFloodIsRefusedAfterTheBurst) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());

  // Six lobby-less getRoomState frames spend the burst; each earns the
  // ordinary rejection, proving real handling happened.
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(
        alice->stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
    auto rejected = NextEvent(alice->stream);
    ASSERT_TRUE(rejected.has_value());
    ASSERT_NE(rejected->as_commandRejected_or_null(), nullptr);
    EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");
  }

  ASSERT_TRUE(
      alice->stream.Send(GolfCommands::FromGetroomstate(moonbase::games::GetRoomState{})).ok());
  auto limited = NextEvent(alice->stream);
  ASSERT_TRUE(limited.has_value());
  ASSERT_NE(limited->as_commandRejected_or_null(), nullptr);
  EXPECT_EQ(limited->as_commandRejected_or_null()->reason, "slow down");
  EXPECT_EQ(metrics_->CounterTotal("golf_rate_limited", {{"kind", "command"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("golf_rejections", {{"kind", "rate_limited"}}), 1);
}

}  // namespace
}  // namespace games_hub
