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
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/hub_handler.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/pg_hub_store.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
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

// Receives until an event of the wanted case arrives (skipping others),
// failing after a few frames so a wrong stream can't hang the test.
inline std::optional<moonbase::golf::GolfEvents> ReceiveCase(
    moonbase::golf::PlayClientStream& stream, const std::string& wanted) {
  for (int i = 0; i < 8; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    if (wanted == (*received)->case_name()) return **received;
  }
  return std::nullopt;
}

// Same, but tunnels into the golf envelope: returns the first GolfUpdate
// of the wanted case, skipping room noise and other updates in between.
inline std::optional<moonbase::golf::GolfUpdate> ReceiveGolf(
    moonbase::golf::PlayClientStream& stream, const std::string& wanted) {
  for (int i = 0; i < 16; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    const auto* envelope = (*received)->as_golf_or_null();
    if (envelope == nullptr) continue;
    if (wanted == envelope->update.case_name()) return envelope->update;
  }
  return std::nullopt;
}

inline moonbase::golf::GolfCommands Move(moonbase::golf::GolfMove move) {
  moonbase::golf::GolfCommand command;
  command.move = std::move(move);
  return moonbase::golf::GolfCommands::FromGolf(std::move(command));
}

class GolfHubStreamFixture : public testing::Test {
 protected:
  // The persistence seams (#1194): the default fixture stays the blessed
  // all-in-memory hub; the pg e2e suite overrides both to run the same
  // flows with durable credentials and the rooms/games write-through.
  virtual std::shared_ptr<TicketVault> MakeVault() {
    return std::make_shared<InMemoryTicketVault>(/*ticket_ttl=*/std::chrono::seconds(60),
                                                 /*resume_ttl=*/std::chrono::seconds(60));
  }
  virtual std::shared_ptr<PgHubStore> MakeStore() { return nullptr; }

  void SetUp() override { BuildHub(); }

  void BuildHub() {
    vault_ = MakeVault();
    store_ = MakeStore();
    // NoShuffleDealer: hands are dealt from the back of the pristine deck,
    // so every card in every test is known (first seat gets the aces).
    // Sequential ids keep players and game codes readable in failures.
    // The recorder rides the global (no-op) meter here — values go nowhere,
    // but every counting path runs under the e2e suite.
    handler_ = std::make_shared<HubHandler>(
        vault_, std::make_shared<cards::NoShuffleDealer>(), ids_,
        /*grace_period=*/std::chrono::seconds(60),
        std::make_shared<futility::otel::MetricsRecorder>("golf_hub_test"), store_);
    if (store_ != nullptr) {
      const absl::Status restored = handler_->RestoreFromStore();
      ASSERT_TRUE(restored.ok()) << restored;
    }
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
  std::shared_ptr<PgHubStore> store_;
  std::shared_ptr<IdGenerator> ids_ = std::make_shared<SequentialIdGenerator>();
  std::shared_ptr<HubHandler> handler_;
  std::unique_ptr<moonbase::golf::GolfHubServer> server_;
  std::unique_ptr<moonbase::golf::GolfHubClient> client_;
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions_;
};

}  // namespace golf_hub

#endif
