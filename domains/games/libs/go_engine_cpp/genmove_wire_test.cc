// Beyoncé Rule wire-contract test (consumer tier), as chess_engine_cpp's
// best_move_wire_test: the request the hub puts on the wire and the answer
// go_engine gives, pinned as bytes. The pins are go_engine's own
// `TestTheWireIsPinned` fixtures (domains/games/apis/go_engine), character
// for character.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/libs/go_engine_cpp/client.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

constexpr char kPinnedRequest[] = R"({"komi":6.5,"level":7,"moves":["E5","pass"],"size":9})";
constexpr char kPinnedScoring[] =
    R"({"komi":6.5,"level":7,"moves":["E5","pass","pass"],"scoring":true,"size":9})";
constexpr char kPinnedAnswer[] = "{\"answer\":\"C3\"}\n";

class Scripted final : public opal::http::HttpClient {
 public:
  Scripted(int status, std::string body) : status_(status), body_(std::move(body)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    requests_.push_back(request);
    opal::http::HttpResponse response;
    response.status = status_;
    response.body = body_;
    return response;
  }

  const std::vector<opal::http::HttpRequest>& requests() const { return requests_; }

 private:
  int status_;
  std::string body_;
  std::vector<opal::http::HttpRequest> requests_;
};

struct Call {
  std::shared_ptr<Scripted> transport;
  opal::Outcome<std::string> answer;
};

Call Ask(int status, std::string body, go_engine::Ask ask) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = go_engine::DefaultClientConfig("http://go_engine:8095");
  config.http_client = transport;
  auto client = go_engine::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {transport, client->Genmove(ask)};
}

TEST(GenmoveWire, SendsThePinnedRequest) {
  const Call call = Ask(200, kPinnedAnswer, {9, 6.5, {"E5", "pass"}, 7, false});
  ASSERT_EQ(call.transport->requests().size(), 1u);
  const auto& request = call.transport->requests()[0];
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.target, "/go_engine/v1/genmove");
  EXPECT_EQ(request.body, kPinnedRequest);
  EXPECT_EQ(request.headers.Get("user-agent").value_or(""), "games_hub/1.0");
}

TEST(GenmoveWire, TheScoringSaysSo) {
  const Call call = Ask(200, kPinnedAnswer, {9, 6.5, {"E5", "pass", "pass"}, 7, true});
  ASSERT_EQ(call.transport->requests().size(), 1u);
  EXPECT_EQ(call.transport->requests()[0].body, kPinnedScoring);
}

TEST(GenmoveWire, ParsesTheAnswer) {
  const Call call = Ask(200, kPinnedAnswer, {9, 6.5, {}, 7, false});
  ASSERT_TRUE(call.answer.ok()) << call.answer.error().message();
  EXPECT_EQ(*call.answer, "C3");
}

TEST(GenmoveWire, AnEngineThatCouldNotAnswerIsAnError) {
  for (const int status : {422, 503}) {
    const Call call = Ask(status, R"({"status":503})", {9, 6.5, {}, 7, false});
    EXPECT_FALSE(call.answer.ok()) << status;
  }
}

TEST(GenmoveWire, TheDeadlineOutlastsTheService) {
  EXPECT_EQ(go_engine::DefaultClientConfig("http://x").request_timeout_ms, 12'000);
  EXPECT_EQ(go_engine::DefaultClientConfig("http://x").retry.max_attempts, 1);
}

}  // namespace
