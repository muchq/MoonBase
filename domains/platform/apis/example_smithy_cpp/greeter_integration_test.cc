// Phase 0 spike tests (https://github.com/muchq/MoonBase/issues/1168): the
// generated client drives the generated server over the loopback transport
// and a real socket, covering the seams portrait relies on — happy path,
// @default handling, constraint validation, modeled errors, and the
// health-endpoint middleware.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "moonbase/greeter/client.h"
#include "moonbase/greeter/server.h"
#include "smithy/client/config.h"
#include "smithy/http/loopback.h"
#include "smithy/http/socket_transport.h"
#include "smithy/server/middleware.h"

namespace {

using moonbase::greeter::GreeterClient;
using moonbase::greeter::GreeterHandler;
using moonbase::greeter::GreeterServer;
using moonbase::greeter::GreetInput;
using moonbase::greeter::GreetOutput;
using moonbase::greeter::UnwelcomeGuest;

class GreetingHandler final : public GreeterHandler {
 public:
  smithy::Outcome<GreetOutput> Greet(const GreetInput& input) override {
    if (input.name == "grinch") {
      const std::string message = "not welcome here: " + input.name;
      smithy::Error error = smithy::Error::Modeled("UnwelcomeGuest", message);
      error.set_detail(UnwelcomeGuest{.message = message});
      return error;
    }
    return GreetOutput{.message =
                           "hello, " + input.name +
                           std::string(static_cast<size_t>(input.enthusiasm.value_or(1)), '!')};
  }
};

enum class Transport { kLoopback, kSocket };

class GreeterIntegrationTest : public ::testing::TestWithParam<Transport> {
 protected:
  void SetUp() override {
    server_ = std::make_unique<GreeterServer>(std::make_shared<GreetingHandler>());
    smithy::ClientConfig config;
    if (GetParam() == Transport::kLoopback) {
      auto loopback = std::make_shared<smithy::http::Loopback>();
      ASSERT_TRUE(loopback->Start(server_->Handler()).ok());
      config.http_client = loopback;
    } else {
      socket_server_ = std::make_unique<smithy::http::SocketHttpServer>();
      ASSERT_TRUE(socket_server_->Start(server_->Handler()).ok());
      config.endpoint = "http://127.0.0.1:" + std::to_string(socket_server_->port());
    }
    auto client = GreeterClient::Create(std::move(config));
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<GreeterClient>(std::move(*client));
  }

  void TearDown() override {
    if (socket_server_ != nullptr) socket_server_->Stop();
  }

  std::unique_ptr<GreeterServer> server_;
  std::unique_ptr<smithy::http::SocketHttpServer> socket_server_;
  std::unique_ptr<GreeterClient> client_;
};

TEST_P(GreeterIntegrationTest, GreetRoundTripsAndAppliesDefault) {
  // enthusiasm is omitted: the @default of 1 must be applied.
  const auto greeted = client_->Greet(GreetInput{.name = "moon"});
  ASSERT_TRUE(greeted.ok()) << greeted.error().message();
  EXPECT_EQ(greeted->message, "hello, moon!");
}

TEST_P(GreeterIntegrationTest, ExplicitEnthusiasmIsHonored) {
  const auto greeted = client_->Greet(GreetInput{.name = "moon", .enthusiasm = 3});
  ASSERT_TRUE(greeted.ok()) << greeted.error().message();
  EXPECT_EQ(greeted->message, "hello, moon!!!");
}

TEST_P(GreeterIntegrationTest, ConstraintValidationRejectsBeforeTheHandler) {
  // @length(min: 1) on name: the framework answers 400 ValidationException
  // without invoking the handler.
  const auto rejected = client_->Greet(GreetInput{.name = ""});
  ASSERT_FALSE(rejected.ok());

  // @range(max: 10) on enthusiasm.
  const auto too_keen = client_->Greet(GreetInput{.name = "moon", .enthusiasm = 11});
  ASSERT_FALSE(too_keen.ok());
}

TEST_P(GreeterIntegrationTest, ModeledErrorsSurfaceTyped) {
  const auto denied = client_->Greet(GreetInput{.name = "grinch"});
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "UnwelcomeGuest");
  ASSERT_NE(denied.error().detail<UnwelcomeGuest>(), nullptr);
  EXPECT_EQ(denied.error().detail<UnwelcomeGuest>()->message, "not welcome here: grinch");
}

INSTANTIATE_TEST_SUITE_P(Transports, GreeterIntegrationTest,
                         ::testing::Values(Transport::kLoopback, Transport::kSocket),
                         [](const auto& info) {
                           return info.param == Transport::kLoopback ? "Loopback" : "Socket";
                         });

TEST(GreeterMiddlewareTest, HealthEndpointComposesAroundTheServer) {
  GreeterServer server(std::make_shared<GreetingHandler>());
  auto handler =
      smithy::server::Chain({smithy::server::HealthEndpoint("/health")}, server.Handler());

  auto loopback = std::make_shared<smithy::http::Loopback>();
  ASSERT_TRUE(loopback->Start(handler).ok());

  smithy::http::HttpRequest health;
  health.method = "GET";
  health.target = "/health";
  const auto response = loopback->Send(health);
  ASSERT_TRUE(response.ok());
  EXPECT_EQ(response->status, 200);
  EXPECT_EQ(response->body, R"({"status":"healthy"})");

  // The generated client still works through the chain.
  smithy::ClientConfig config;
  config.http_client = loopback;
  auto client = GreeterClient::Create(std::move(config));
  ASSERT_TRUE(client.ok()) << client.error().message();
  const auto greeted = client->Greet(GreetInput{.name = "moon"});
  ASSERT_TRUE(greeted.ok()) << greeted.error().message();
}

}  // namespace
