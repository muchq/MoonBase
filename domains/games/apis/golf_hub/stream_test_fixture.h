#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_STREAM_TEST_FIXTURE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_STREAM_TEST_FIXTURE_H

// The in-memory e2e fixture, following smithy-cpp's server-guide recipe
// (and examples/chat/stream_test_fixture.h): a generated GolfHubClient
// whose websocket_dialer hands back one end of an InMemoryWebSocketPair,
// serving the other end through the generated StreamRouter's session seam
// (ADR-0021 — the launch point runs inline in the dialer; the pair's
// completions drive the coroutine, no serve thread), with a Loopback
// carrying the unary GetSession.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/hub_handler.h"
#include "domains/games/apis/golf_hub/hub_store.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "moonbase/golf/client.h"
#include "moonbase/golf/server.h"
#include "smithy/client/config.h"
#include "smithy/core/outcome.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

namespace golf_hub {

// How long a receive helper waits in total for the event it wants. The
// frame counts below bound the wrong-events case; this bounds the case
// they cannot see — an expectation for an event the hub never sends,
// which without a deadline blocks inside Receive() forever and burns the
// test's whole timeout with nothing to show for it.
inline constexpr std::chrono::milliseconds kReceiveBudget{5000};

// Spends what is left of `budget` on one receive, or reports the timeout
// itself when the budget is gone. Feeding the remainder to each call
// keeps the helper's total wait bounded no matter how many frames arrive.
inline auto ReceiveWithin(moonbase::golf::PlayClientStream& stream,
                          std::chrono::steady_clock::time_point deadline) {
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now());
  // A non-positive deadline polls rather than blocks, which is what we
  // want on the last look: take an event already in hand, else time out.
  return stream.Receive(remaining);
}

// Receives until an event of the wanted case arrives (skipping others),
// giving up after a few frames so a wrong stream can't hang the test, or
// after kReceiveBudget so a silent one can't either.
inline std::optional<moonbase::golf::GolfEvents> ReceiveCase(
    moonbase::golf::PlayClientStream& stream, const std::string& wanted,
    std::chrono::milliseconds budget = kReceiveBudget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  for (int i = 0; i < 8; ++i) {
    auto received = ReceiveWithin(stream, deadline);
    if (!received.ok()) {
      // Name the event nobody sent: the whole point of the deadline is a
      // test that says what it was waiting for instead of hanging.
      ADD_FAILURE() << "gave up waiting for " << wanted << ": " << received.error().message();
      return std::nullopt;
    }
    if (!received->has_value()) return std::nullopt;
    if (wanted == (*received)->case_name()) return **received;
  }
  return std::nullopt;
}

// One frame, bounded, no skipping — for asserting exact event order.
// ReceiveCase would silently step over an event that must not be there.
inline std::optional<moonbase::golf::GolfEvents> NextEvent(
    moonbase::golf::PlayClientStream& stream, std::chrono::milliseconds budget = kReceiveBudget) {
  auto received = stream.Receive(budget);
  if (!received.ok()) {
    ADD_FAILURE() << "receive failed mid-sequence: " << received.error().message();
    return std::nullopt;
  }
  if (!received->has_value()) {
    ADD_FAILURE() << "stream closed mid-sequence";
    return std::nullopt;
  }
  return **received;
}

// Same, but tunnels into the golf envelope: returns the first GolfUpdate
// of the wanted case, skipping room noise and other updates in between.
inline std::optional<moonbase::golf::GolfUpdate> ReceiveGolf(
    moonbase::golf::PlayClientStream& stream, const std::string& wanted,
    std::chrono::milliseconds budget = kReceiveBudget) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  for (int i = 0; i < 16; ++i) {
    auto received = ReceiveWithin(stream, deadline);
    if (!received.ok()) {
      ADD_FAILURE() << "gave up waiting for golf " << wanted << ": " << received.error().message();
      return std::nullopt;
    }
    if (!received->has_value()) return std::nullopt;
    const auto* envelope = (*received)->as_golf_or_null();
    if (envelope == nullptr) continue;
    if (wanted == envelope->update.case_name()) return envelope->update;
  }
  return std::nullopt;
}

// Effectively-unlimited stream budgets (#1240) for suites whose flows
// send at test speed, not human speed. Every direct HubHandler
// construction in a test should pass this unless the test is about the
// limiter itself.
inline RateLimits UnlimitedRateLimits() {
  RateLimits limits;
  limits.command_burst = 1e9;
  limits.command_refill_per_sec = 1e9;
  limits.chat_burst = 1e9;
  limits.chat_refill_per_sec = 1e9;
  return limits;
}

inline moonbase::golf::GolfCommands Move(moonbase::golf::GolfMove move) {
  moonbase::golf::GolfCommand command;
  command.move = std::move(move);
  return moonbase::golf::GolfCommands::FromGolf(std::move(command));
}

// Captures every metric the hub records so tests can assert what is
// counted — and, just as important, what never appears in a name or
// label (room ids, player ids, message text; the model forbids them).
// The recorder itself is shared platform tooling; only the service name
// is the hub's.
using futility::otel::CapturingMetricsRecorder;

inline std::shared_ptr<CapturingMetricsRecorder> MakeCapturingMetricsRecorder() {
  return std::make_shared<CapturingMetricsRecorder>("golf_hub_test");
}

class GolfHubStreamFixture : public testing::Test {
 protected:
  // The default fixture uses the production memory implementations; the
  // pg e2e suite overrides both to run the same flows against PostgreSQL.
  virtual std::shared_ptr<TicketVault> MakeVault() {
    return std::make_shared<InMemoryTicketVault>(/*ticket_ttl=*/std::chrono::seconds(60),
                                                 /*resume_ttl=*/std::chrono::seconds(60));
  }
  virtual std::shared_ptr<HubStore> MakeStore() { return std::make_shared<MemoryHubStore>(); }
  /// Null selects the default: a MemoryChatStore authorized through the
  /// handler's membership guard, wired up two-phase below. The pg suite
  /// overrides with PgChatStore, which authorizes in its own transaction
  /// and needs nothing from the handler.
  virtual std::shared_ptr<ChatStore> MakeChatStore() { return nullptr; }
  /// The stream budgets (#1240). Tests get effectively-unlimited
  /// buckets: e2e flows blast frames far faster than any human, and only
  /// the rate-limit suite wants refusals — it overrides with tiny
  /// buckets whose refills are frozen.
  virtual RateLimits MakeRateLimits() { return UnlimitedRateLimits(); }
  /// ADR-0020 reconnect grace — long enough that no test sees an expiry
  /// by accident; the expiry suites override it down to something a
  /// receive budget can wait out.
  virtual std::chrono::seconds GracePeriod() { return std::chrono::seconds(60); }

  void SetUp() override { BuildHub(); }

  void BuildHub() {
    vault_ = MakeVault();
    store_ = MakeStore();
    // NoShuffleDealer: hands are dealt from the back of the pristine deck,
    // so every card in every test is known (first seat gets the aces).
    // Sequential ids keep players and game codes readable in failures.
    // The recorder rides the global (no-op) meter here — values go nowhere,
    // but every counting path runs under the e2e suite.
    // The same MemoryChatStore the handler would build for itself, but
    // held here so tests can see what chat actually stored. The guard is
    // filled in after construction because it calls the handler that
    // does not exist yet; nothing appends before then.
    auto guard = std::make_shared<MemberGuard>();
    chat_store_ = MakeChatStore();
    const bool default_memory_chat = chat_store_ == nullptr;
    if (default_memory_chat) {
      chat_store_ = std::make_shared<MemoryChatStore>(
          [guard](const std::string& room_id, const std::string& player_id,
                  const MemberAction& action) { return (*guard)(room_id, player_id, action); });
    }
    handler_ = std::make_shared<HubHandler>(
        vault_, std::make_shared<cards::NoShuffleDealer>(), ids_,
        /*grace_period=*/GracePeriod(), metrics_, store_, chat_store_, MakeRateLimits());
    if (default_memory_chat) {
      *guard = [handler = handler_.get()](const std::string& room_id, const std::string& player_id,
                                          const MemberAction& action) {
        return handler->WithMember(room_id, player_id, action);
      };
    }
    const absl::Status restored = handler_->RestoreFromStore();
    ASSERT_TRUE(restored.ok()) << restored;
    server_ = std::make_unique<moonbase::golf::GolfHubServer>(handler_);

    auto loopback = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback->Start(server_->Handler()).ok());

    smithy::ClientConfig config;
    config.retry.max_attempts = 1;
    config.http_client = loopback;
    config.websocket_dialer = [this](const smithy::http::WebSocketDialRequest& request)
        -> smithy::Outcome<std::shared_ptr<smithy::http::WebSocket>> {
      auto [near, far] = smithy::http::InMemoryWebSocketPair::Create();
      smithy::http::HttpRequest upgrade;
      upgrade.method = "GET";
      upgrade.target = request.target;
      upgrade.headers = request.headers;
      sessions_.push_back(far);
      server_->StreamRouter()->ServeSession()(upgrade, far);
      return near;
    };
    auto client = moonbase::golf::GolfHubClient::Create(std::move(config));
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<moonbase::golf::GolfHubClient>(std::move(*client));
  }

  void TearDown() override {
    // Idempotent; unblocks any session a failed test body left parked so
    // the registry's teardown joins cannot hang.
    for (auto& session : sessions_) session->Close();
  }

  // Mint a session and open its Play stream; fails the test on any step.
  // The client-parameterized form serves multi-instance suites (#1194
  // step 3), where each hub instance has its own client.
  struct Seat {
    std::string player_id;
    std::string resume_token;
    moonbase::golf::PlayClientStream stream;
  };
  std::optional<Seat> OpenSeatVia(moonbase::golf::GolfHubClient& client,
                                  const std::optional<std::string>& resume_token = std::nullopt) {
    moonbase::golf::GetSessionInput session_input;
    if (resume_token.has_value()) session_input.resumeToken = *resume_token;
    auto session = client.GetSession(session_input);
    if (!session.ok()) {
      ADD_FAILURE() << "GetSession failed: " << session.error().message();
      return std::nullopt;
    }
    moonbase::golf::PlayInput play_input;
    play_input.ticket = session->ticket;
    auto stream = client.Play(play_input);
    if (!stream.ok()) {
      ADD_FAILURE() << "Play dial failed: " << stream.error().message();
      return std::nullopt;
    }
    return Seat{session->playerId, session->resumeToken, std::move(*stream)};
  }
  std::optional<Seat> OpenSeat(const std::optional<std::string>& resume_token = std::nullopt) {
    return OpenSeatVia(*client_, resume_token);
  }

  // The create-room preamble a dozen tests otherwise spell out: sends
  // CreateRoom from an already-ready seat, receives the creator's
  // roomState, and returns the room id — empty (with a failure already
  // recorded) when any step misbehaves.
  std::string CreateRoomFor(Seat& seat) {
    if (!seat.stream
             .Send(moonbase::golf::GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{}))
             .ok()) {
      ADD_FAILURE() << "CreateRoom send failed";
      return "";
    }
    auto created = ReceiveCase(seat.stream, "roomState");
    if (!created.has_value()) {
      ADD_FAILURE() << "no roomState after CreateRoom";
      return "";
    }
    return created->as_roomState_or_null()->roomId;
  }

  // Room with two seats in a started game: with the NoShuffleDealer the
  // first seat holds the aces and the second the kings, Q♠ seeding the
  // discard. Returns nullopt (with an ADD_FAILURE already recorded by the
  // failing step where possible) when any step misbehaves.
  struct Table {
    Seat alice;
    Seat bob;
    std::string room_id;
    std::string game_id;
  };
  std::optional<Table> SeatedTable() {
    auto alice = OpenSeat();
    auto bob = OpenSeat();
    if (!alice.has_value() || !bob.has_value()) return std::nullopt;
    if (!ReceiveCase(alice->stream, "sessionReady").has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "sessionReady").has_value()) return std::nullopt;

    if (!alice->stream
             .Send(moonbase::golf::GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{}))
             .ok()) {
      return std::nullopt;
    }
    auto created = ReceiveCase(alice->stream, "roomState");
    if (!created.has_value()) return std::nullopt;
    const std::string room_id = created->as_roomState_or_null()->roomId;
    moonbase::golf::JoinRoom join_room;
    join_room.roomId = room_id;
    if (!bob->stream.Send(moonbase::golf::GolfCommands::FromJoinroom(join_room)).ok())
      return std::nullopt;
    if (!ReceiveCase(bob->stream, "roomState").has_value()) return std::nullopt;

    if (!alice->stream
             .Send(Move(moonbase::golf::GolfMove::FromCreategame(moonbase::golf::CreateGame{})))
             .ok()) {
      return std::nullopt;
    }
    auto joined = ReceiveGolf(alice->stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;

    moonbase::golf::JoinGame join_game;
    join_game.gameId = game_id;
    if (!bob->stream.Send(Move(moonbase::golf::GolfMove::FromJoingame(join_game))).ok())
      return std::nullopt;
    if (!ReceiveGolf(bob->stream, "gameJoined").has_value()) return std::nullopt;

    if (!alice->stream
             .Send(Move(moonbase::golf::GolfMove::FromStartgame(moonbase::golf::StartGame{})))
             .ok()) {
      return std::nullopt;
    }
    if (!ReceiveGolf(alice->stream, "gameStarted").has_value()) return std::nullopt;
    if (!ReceiveGolf(bob->stream, "gameStarted").has_value()) return std::nullopt;
    return Table{std::move(*alice), std::move(*bob), room_id, game_id};
  }

  std::shared_ptr<TicketVault> vault_;
  std::shared_ptr<HubStore> store_;
  std::shared_ptr<ChatStore> chat_store_;
  // Every suite gets capture; only the metrics tests assert on it. The
  // no-op meter underneath means values still go nowhere.
  std::shared_ptr<CapturingMetricsRecorder> metrics_ = MakeCapturingMetricsRecorder();
  std::shared_ptr<IdGenerator> ids_ = std::make_shared<SequentialIdGenerator>();
  std::shared_ptr<HubHandler> handler_;
  std::unique_ptr<moonbase::golf::GolfHubServer> server_;
  std::unique_ptr<moonbase::golf::GolfHubClient> client_;
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions_;
};

}  // namespace golf_hub

#endif
