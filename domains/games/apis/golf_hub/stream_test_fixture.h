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
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/hub_handler.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "moonbase/golf/client.h"
#include "moonbase/golf/server.h"
#include "smithy/client/config.h"
#include "smithy/core/outcome.h"
#include "smithy/http/loopback.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"

namespace golf_hub {

class GolfHubStreamFixture : public testing::Test {
 protected:
  void SetUp() override {
    vault_ = std::make_shared<TicketVault>(/*ticket_ttl=*/std::chrono::seconds(60),
                                           /*resume_ttl=*/std::chrono::seconds(60));
    // NoShuffleDealer: hands are dealt from the back of the pristine deck,
    // so every card in every test is known (first seat gets the aces).
    handler_ = std::make_shared<HubHandler>(vault_, std::make_shared<cards::NoShuffleDealer>(),
                                            /*grace_period=*/std::chrono::seconds(60));
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
  struct Seat {
    std::string player_id;
    std::string resume_token;
    moonbase::golf::PlayClientStream stream;
  };
  std::optional<Seat> OpenSeat(const std::optional<std::string>& resume_token = std::nullopt) {
    moonbase::golf::GetSessionInput session_input;
    if (resume_token.has_value()) session_input.resumeToken = *resume_token;
    auto session = client_->GetSession(session_input);
    if (!session.ok()) {
      ADD_FAILURE() << "GetSession failed: " << session.error().message();
      return std::nullopt;
    }
    moonbase::golf::PlayInput play_input;
    play_input.ticket = session->ticket;
    auto stream = client_->Play(play_input);
    if (!stream.ok()) {
      ADD_FAILURE() << "Play dial failed: " << stream.error().message();
      return std::nullopt;
    }
    return Seat{session->playerId, session->resumeToken, std::move(*stream)};
  }

  std::shared_ptr<TicketVault> vault_;
  std::shared_ptr<HubHandler> handler_;
  std::unique_ptr<moonbase::golf::GolfHubServer> server_;
  std::unique_ptr<moonbase::golf::GolfHubClient> client_;
  std::vector<std::shared_ptr<smithy::http::WebSocket>> sessions_;
};

}  // namespace golf_hub

#endif
