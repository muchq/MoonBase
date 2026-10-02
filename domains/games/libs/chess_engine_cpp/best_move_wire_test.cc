// Beyoncé Rule wire-contract test (consumer tier), as mithril_cpp's
// wordchain_wire_test: the request the hub puts on the wire and the answer
// chess_engine gives, pinned as bytes. The pins are chess_engine's own
// `TestTheWireIsPinned` fixtures (domains/games/apis/chess_engine),
// character for character.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/libs/chess_engine_cpp/client.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

constexpr char kPinnedRequest[] =
    R"({"elo":1500,"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","moves":["g6f6","h8g8"],"movetimeMs":300})";
constexpr char kPinnedFullStrength[] = R"({"fen":"7k/4P3/6K1/8/8/8/8/8 w - - 0 1","movetimeMs":300})";
constexpr char kPinnedAnswer[] = "{\"uci\":\"e7e8q\"}\n";

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
  opal::Outcome<std::string> move;
};

Call Ask(int status, std::string body, chess_engine::Ask ask) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = chess_engine::DefaultClientConfig("http://chess_engine:8094");
  config.http_client = transport;
  auto client = chess_engine::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {transport, client->BestMove(ask)};
}

chess_engine::Ask Kpk(std::optional<int> elo, std::vector<std::string> moves) {
  return {"7k/4P3/6K1/8/8/8/8/8 w - - 0 1", std::move(moves), 300, elo};
}

TEST(BestMoveWire, SendsThePinnedRequest) {
  const Call call = Ask(200, kPinnedAnswer, Kpk(1500, {"g6f6", "h8g8"}));
  ASSERT_EQ(call.transport->requests().size(), 1u);
  const auto& request = call.transport->requests()[0];
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.target, "/chess_engine/v1/bestmove");
  EXPECT_EQ(request.body, kPinnedRequest);
  EXPECT_EQ(request.headers.Get("user-agent").value_or(""), "games_hub/1.0");
}

// Full strength and the starting position say so by leaving fields out.
TEST(BestMoveWire, FullStrengthWithNoMovesSendsNeither) {
  const Call call = Ask(200, kPinnedAnswer, Kpk(std::nullopt, {}));
  ASSERT_EQ(call.transport->requests().size(), 1u);
  EXPECT_EQ(call.transport->requests()[0].body, kPinnedFullStrength);
}

TEST(BestMoveWire, ParsesTheMove) {
  const Call call = Ask(200, kPinnedAnswer, Kpk(1500, {}));
  ASSERT_TRUE(call.move.ok()) << call.move.error().message();
  EXPECT_EQ(*call.move, "e7e8q");
}

TEST(BestMoveWire, AnEngineThatCouldNotAnswerIsAnError) {
  for (const int status : {422, 503}) {
    const Call call = Ask(status, R"({"status":503})", Kpk(std::nullopt, {}));
    EXPECT_FALSE(call.move.ok()) << status;
  }
}

// The deadline covers the service's own: MaxMovetimeMs (5 s) plus its
// 2 s of slack, with a little left for the connection.
TEST(BestMoveWire, TheDeadlineOutlastsTheService) {
  EXPECT_EQ(chess_engine::DefaultClientConfig("http://x").request_timeout_ms, 8'000);
  EXPECT_EQ(chess_engine::DefaultClientConfig("http://x").retry.max_attempts, 1);
}

}  // namespace
