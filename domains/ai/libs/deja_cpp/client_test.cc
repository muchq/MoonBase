// The tape client's own behavior: what it puts on the wire, what it does
// with an answer, and what it does when deja is having a bad day. The
// bytes deja actually sends are recent_wire_test's pin.

#include "domains/ai/libs/deja_cpp/client.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

using deja::Client;

class ScriptedHttpClient final : public opal::http::HttpClient {
 public:
  explicit ScriptedHttpClient(std::vector<opal::http::HttpResponse> responses)
      : responses_(std::move(responses)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    requests_.push_back(request);
    if (next_response_ == responses_.size()) {
      return opal::Error::Unknown("deja is not answering");
    }
    return responses_[next_response_++];
  }

  const std::vector<opal::http::HttpRequest>& requests() const { return requests_; }

 private:
  std::vector<opal::http::HttpResponse> responses_;
  std::vector<opal::http::HttpRequest> requests_;
  std::size_t next_response_ = 0;
};

opal::http::HttpResponse Ok(std::string body) {
  opal::http::HttpResponse response;
  response.status = 200;
  response.body = std::move(body);
  return response;
}

Client Dial(std::shared_ptr<ScriptedHttpClient> transport) {
  opal::ClientConfig config = deja::DefaultClientConfig("http://deja:8093");
  config.http_client = std::move(transport);
  auto client = Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return std::move(*client);
}

// A poller is not an indexer: a tape event missed is one the next tick
// fetches anyway, so the config buys a fast failure rather than a retry
// that holds a thread past its own next tick.
TEST(DefaultClientConfigTest, PollsOnceAndGivesUpFast) {
  const opal::ClientConfig config = deja::DefaultClientConfig("http://deja:8093");

  EXPECT_EQ(config.endpoint, "http://deja:8093");
  EXPECT_EQ(config.user_agent, "MoonBase games_hub/1.0");
  EXPECT_EQ(config.request_timeout_ms, 2'000);
  EXPECT_EQ(config.retry.max_attempts, 1);
}

TEST(ClientTest, RecentAsksForEverythingAfterTheSeqItWasGiven) {
  auto transport = std::make_shared<ScriptedHttpClient>(
      std::vector<opal::http::HttpResponse>{Ok(R"({"events":[]})"), Ok(R"({"events":[]})")});
  const Client client = Dial(transport);

  ASSERT_TRUE(client.Recent(41).ok());
  ASSERT_TRUE(client.Recent(0).ok());

  ASSERT_EQ(transport->requests().size(), 2u);
  EXPECT_EQ(transport->requests()[0].method, "GET");
  EXPECT_EQ(transport->requests()[0].target, "/deja/v1/recent?after=41");
  // Zero is a real ask — "everything the ring has" — not an omission.
  EXPECT_EQ(transport->requests()[1].target, "/deja/v1/recent?after=0");
  EXPECT_EQ(transport->requests()[0].headers.Get("user-agent").value_or(""),
            "MoonBase games_hub/1.0");
}

TEST(ClientTest, NothingNewIsASuccessWithNoEvents) {
  auto transport = std::make_shared<ScriptedHttpClient>(
      std::vector<opal::http::HttpResponse>{Ok(R"({"events":[]})")});

  const auto recent = Dial(transport).Recent(7);

  ASSERT_TRUE(recent.ok()) << recent.error().message();
  EXPECT_TRUE(recent->events.empty());
}

// deja down, deja slow, deja 500ing: each is an error the caller shrugs
// at, never a throw and never a partial answer.
TEST(ClientTest, ADeadOrAngryDejaIsAnErrorAndNothingElse) {
  auto unreachable = std::make_shared<ScriptedHttpClient>(std::vector<opal::http::HttpResponse>{});
  EXPECT_FALSE(Dial(unreachable).Recent(0).ok());
  EXPECT_EQ(unreachable->requests().size(), 1u) << "a poll must not retry";

  opal::http::HttpResponse broken;
  broken.status = 500;
  broken.body = R"({"message":"engine lock"})";
  auto failing =
      std::make_shared<ScriptedHttpClient>(std::vector<opal::http::HttpResponse>{broken});
  EXPECT_FALSE(Dial(failing).Recent(0).ok());
}

TEST(ClientTest, GarbageInsteadOfTapeIsAnErrorNotAnEmptyTape) {
  for (const char* body : {"", "not json", "[]", R"({"events":3})", R"({"nope":[]})"}) {
    auto transport =
        std::make_shared<ScriptedHttpClient>(std::vector<opal::http::HttpResponse>{Ok(body)});

    const auto recent = Dial(transport).Recent(0);

    EXPECT_FALSE(recent.ok()) << body << " parsed as a tape";
  }
}

}  // namespace
