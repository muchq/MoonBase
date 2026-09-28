// Beyoncé Rule wire-contract test (consumer tier), as microgpt_cpp's
// chat_wire_test: the request the hub puts on the wire and the answers
// mithril gives, pinned as bytes. The pins are mithril's own
// `wordchain_*_is_pinned_on_the_wire` fixtures
// (domains/games/apis/mithril/src/main.rs), character for character.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/libs/mithril_cpp/client.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

constexpr char kPinnedRequest[] = R"({"end":"warm","start":"cold"})";
constexpr char kPinnedFound[] = R"({"path":["cold","cord","card","ward","warm"]})";
constexpr char kPinnedNone[] = R"({"path":null})";

// Answers every request with one scripted response and keeps what it sent.
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
  opal::Outcome<std::optional<std::vector<std::string>>> path;
};

Call Ask(int status, std::string body) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = mithril::DefaultClientConfig("http://mithril:8083");
  config.http_client = transport;
  auto client = mithril::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {transport, client->Wordchain("cold", "warm")};
}

TEST(WordchainWire, SendsThePinnedRequest) {
  const Call call = Ask(200, kPinnedFound);

  ASSERT_EQ(call.transport->requests().size(), 1u);
  const auto& request = call.transport->requests()[0];
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.target, "/mithril/v1/wordchain");
  EXPECT_EQ(request.body, kPinnedRequest);
  // mithril counts its callers by this first product token.
  EXPECT_EQ(request.headers.Get("user-agent").value_or(""), "games_hub/1.0");
}

TEST(WordchainWire, ParsesAFoundLadder) {
  const Call call = Ask(200, kPinnedFound);

  ASSERT_TRUE(call.path.ok()) << call.path.error().message();
  EXPECT_EQ(*call.path, (std::vector<std::string>{"cold", "cord", "card", "ward", "warm"}));
}

TEST(WordchainWire, ANullPathIsNoLadder) {
  const Call call = Ask(200, kPinnedNone);

  ASSERT_TRUE(call.path.ok()) << call.path.error().message();
  EXPECT_FALSE(call.path->has_value());
}

TEST(WordchainWire, RefusalsAreErrorsAndAreNotRetried) {
  for (const int status : {400, 429, 503}) {
    const Call call = Ask(status, R"({"error":"no"})");

    EXPECT_FALSE(call.path.ok()) << status;
    EXPECT_EQ(call.transport->requests().size(), 1u) << status;
  }
}

}  // namespace
