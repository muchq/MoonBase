// Beyoncé Rule wire-contract test (consumer tier), as chess_engine_cpp's
// best_move_wire_test: the index request the hub puts on the wire, pinned
// as bytes against one_d4's IndexRequest record, and the answer it reads.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/libs/one_d4_cpp/client.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

// IndexRequest(player, platform, startMonth, endMonth, excludeBullet,
// skipCache): the last two absent read as false.
constexpr char kPinnedRequest[] =
    R"({"endMonth":"2026-10","platform":"MUCHQ_COM","player":"cheeky-jade-wallaby-yt3z","startMonth":"2026-10"})";
// IndexResponse, trimmed to what the hub reads; the rest is ignored.
constexpr char kPinnedAnswer[] =
    R"({"id":"0b9f7a8e-3c1d-4f2a-9e6b-1a2b3c4d5e6f","player":"cheeky-jade-wallaby-yt3z","platform":"MUCHQ_COM","startMonth":"2026-10","endMonth":"2026-10","status":"PENDING","gamesIndexed":0,"errorMessage":null,"excludeBullet":false,"data":null})";

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
  opal::Outcome<std::string> id;
};

Call Index(int status, std::string body) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = one_d4::DefaultClientConfig("http://one_d4:8080");
  config.http_client = transport;
  auto client = one_d4::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {transport, client->IndexMuchqMonth("cheeky-jade-wallaby-yt3z", "2026-10")};
}

TEST(IndexWire, SendsThePinnedRequest) {
  const Call call = Index(200, kPinnedAnswer);
  ASSERT_EQ(call.transport->requests().size(), 1u);
  const auto& request = call.transport->requests()[0];
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.target, "/v1/index");
  EXPECT_EQ(request.body, kPinnedRequest);
  EXPECT_EQ(request.headers.Get("user-agent").value_or(""), "games_hub/1.0");
}

TEST(IndexWire, ReadsTheRequestsId) {
  const Call call = Index(200, kPinnedAnswer);
  ASSERT_TRUE(call.id.ok()) << call.id.error().message();
  EXPECT_EQ(*call.id, "0b9f7a8e-3c1d-4f2a-9e6b-1a2b3c4d5e6f");
}

TEST(IndexWire, ARefusalIsAnError) {
  for (const int status : {400, 500, 503}) {
    EXPECT_FALSE(Index(status, R"({"message":"no"})").id.ok()) << status;
  }
}

// One attempt, a short deadline: the ask only writes a row.
TEST(IndexWire, OneShortAttempt) {
  EXPECT_EQ(one_d4::DefaultClientConfig("http://x").request_timeout_ms, 5'000);
  EXPECT_EQ(one_d4::DefaultClientConfig("http://x").retry.max_attempts, 1);
}

}  // namespace
