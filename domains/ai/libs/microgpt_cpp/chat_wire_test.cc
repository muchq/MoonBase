// Beyoncé Rule wire-contract test (consumer tier), as deja_cpp's
// recent_wire_test: the request the hub puts on the wire and the answer
// microgpt-serve gives, both pinned as bytes. kPinnedRequest and
// kPinnedResponse are microgpt-serve's own `chat_*_is_pinned_on_the_wire`
// fixtures (domains/ai/apis/microgpt_serve/src/types.rs), character for
// character; the two are edited together, and that is the contract.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "domains/ai/libs/microgpt_cpp/client.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace {

using moonbase::microgpt::Message;

// Keys in the order opal writes them (sorted); serde reads either.
constexpr char kPinnedRequest[] =
    R"({"max_tokens":60,"messages":[{"content":"bouncy-coral-quokka-x9k2: who wins?",)"
    R"("role":"user"},{"content":"the one with the lowest score","role":"assistant"}]})";

constexpr char kPinnedResponse[] =
    R"({"role":"assistant","content":"whoever knocks last","tokens_dropped":3})";

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

std::vector<Message> PinnedMessages() {
  return {Message{.role = "user", .content = "bouncy-coral-quokka-x9k2: who wins?"},
          Message{.role = "assistant", .content = "the one with the lowest score"}};
}

struct Call {
  std::shared_ptr<Scripted> transport;
  opal::Outcome<moonbase::microgpt::ChatOutput> reply;
};

Call Ask(int status, std::string body) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = microgpt::DefaultClientConfig("http://microgpt-serve:8087");
  config.http_client = transport;
  auto client = microgpt::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return {transport, client->Chat(PinnedMessages(), /*max_tokens=*/60)};
}

TEST(ChatWire, SendsThePinnedRequest) {
  const Call call = Ask(200, kPinnedResponse);

  ASSERT_EQ(call.transport->requests().size(), 1u);
  const auto& request = call.transport->requests()[0];
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.target, "/microgpt/v1/chat");
  EXPECT_EQ(request.body, kPinnedRequest);
}

TEST(ChatWire, ParsesThePinnedResponse) {
  const Call call = Ask(200, kPinnedResponse);

  ASSERT_TRUE(call.reply.ok()) << call.reply.error().message();
  EXPECT_EQ(call.reply->role, "assistant");
  EXPECT_EQ(call.reply->content, "whoever knocks last");
  EXPECT_EQ(call.reply->tokensDropped, 3);
}

// A model trained without --chat answers 400, and the per-IP limiter 429:
// both are errors to the caller, tried once.
TEST(ChatWire, RefusalsAreErrorsAndAreNotRetried) {
  for (const int status : {400, 429, 503}) {
    const Call call = Ask(status, R"({"error":"no"})");

    EXPECT_FALSE(call.reply.ok()) << status;
    EXPECT_EQ(call.transport->requests().size(), 1u) << status;
  }
}

}  // namespace
