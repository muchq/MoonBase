// The /wordchain command: what asks, what mithril is asked, and the reply
// text a client reads the ladder back out of.

#include "domains/games/apis/games_hub/wordchain.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/chat_store.h"
#include "domains/games/libs/mithril_cpp/client.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace games_hub {
namespace {

using Path = std::vector<std::string>;

TEST(WordchainCommand, IsTheCommandThenTwoWordsLowercased) {
  const auto ask = WordchainCommand("/wordchain Cold  WARM ");
  ASSERT_TRUE(ask.has_value());
  EXPECT_EQ(ask->start, "cold");
  EXPECT_EQ(ask->end, "warm");
  EXPECT_FALSE(ask->path.has_value());
  EXPECT_TRUE(WordchainCommand("/WORDCHAIN cat dog").has_value());
}

TEST(WordchainCommand, AsksOnlyForTwoWordsOfThreeToNineLetters) {
  for (const char* text :
       {"/wordchain", "/wordchain cold", "/wordchain cold warm hot", "/wordchaincold warm",
        "/wordchain ox dog", "/wordchain cat abcdefghij", "/wordchain c4t dog",
        "/wordchain café dog", "hi /wordchain cat dog", " /wordchain cat dog"}) {
    EXPECT_FALSE(WordchainCommand(text).has_value()) << text;
  }
  EXPECT_TRUE(WordchainCommand("/wordchain abc abcdefghi").has_value());
}

TEST(WordchainText, ALadderIsItsWordsInOrder) {
  EXPECT_EQ(WordchainText({"cold", "warm", Path{"cold", "cord", "card", "ward", "warm"}}),
            "cold → cord → card → ward → warm");
  EXPECT_EQ(WordchainText({"cold", "warm", std::nullopt}), "no ladder from cold to warm");
}

TEST(WordchainOfText, ReadsBackEveryReply) {
  for (const Wordchain& chain :
       {Wordchain{"cold", "warm", Path{"cold", "cord", "card", "ward", "warm"}},
        Wordchain{"cat", "cat", Path{"cat"}}, Wordchain{"cold", "hot", std::nullopt}}) {
    const auto back = WordchainOfText(WordchainText(chain));
    ASSERT_TRUE(back.has_value()) << WordchainText(chain);
    EXPECT_EQ(back->start, chain.start);
    EXPECT_EQ(back->end, chain.end);
    EXPECT_EQ(back->path, chain.path);
  }
}

TEST(WordchainOfText, IsNothingForTextNoReplyWouldHold) {
  for (const char* text : {"", "hello there", "cold → ", "→ warm", "cold →  warm", "Cold → warm",
                           "no ladder from cold", "no ladder from cold to warm!"}) {
    EXPECT_FALSE(WordchainOfText(text).has_value()) << text;
  }
}

// mithril answering every request with one scripted response.
class Scripted final : public opal::http::HttpClient {
 public:
  Scripted(int status, std::string body) : status_(status), body_(std::move(body)) {}

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    bodies_.push_back(request.body);
    if (status_ == 0) return opal::Error::Transport("refused", false);
    opal::http::HttpResponse response;
    response.status = status_;
    response.body = body_;
    return response;
  }

  std::vector<std::string> bodies_;

 private:
  int status_;
  std::string body_;
};

struct Asked {
  std::shared_ptr<Scripted> mithril;
  opal::Outcome<std::string> reply;
};

Asked Ask(const std::string& text, int status, std::string body) {
  auto transport = std::make_shared<Scripted>(status, std::move(body));
  opal::ClientConfig config = mithril::DefaultClientConfig("http://mithril:8083");
  config.http_client = transport;
  auto client = mithril::Client::Create(std::move(config));
  EXPECT_TRUE(client.ok());
  const auto responder = WordchainResponder(std::make_shared<mithril::Client>(std::move(*client)));
  EXPECT_STREQ(responder->Author(), kWordchainPlayerId);
  EXPECT_TRUE(responder->Asks(text));
  ChatRow trigger{.message_id = 7, .room_id = "R1", .player_id = "alice", .text = text};
  return {transport, responder->Reply(trigger)};
}

TEST(WordchainResponder, AsksMithrilForTheCommandsWordsAndRepliesWithTheLadder) {
  const Asked asked =
      Ask("/wordchain COLD warm", 200, R"({"path":["cold","cord","card","ward","warm"]})");

  ASSERT_EQ(asked.mithril->bodies_, std::vector<std::string>{R"({"end":"warm","start":"cold"})"});
  ASSERT_TRUE(asked.reply.ok()) << asked.reply.error().message();
  EXPECT_EQ(*asked.reply, "cold → cord → card → ward → warm");
}

TEST(WordchainResponder, NoLadderIsSaidSo) {
  const Asked asked = Ask("/wordchain cold hot", 200, R"({"path":null})");

  ASSERT_TRUE(asked.reply.ok()) << asked.reply.error().message();
  EXPECT_EQ(*asked.reply, "no ladder from cold to hot");
}

TEST(WordchainResponder, AnUnreachableMithrilIsATransportError) {
  const Asked asked = Ask("/wordchain cold warm", 0, "");

  ASSERT_FALSE(asked.reply.ok());
  EXPECT_EQ(asked.reply.error().kind(), opal::ErrorKind::kTransport);
}

}  // namespace
}  // namespace games_hub
