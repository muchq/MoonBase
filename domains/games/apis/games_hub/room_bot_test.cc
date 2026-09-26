// The room bot (#1591): what counts as a mention, what microgpt is asked,
// what comes back into the room, and what is refused or dropped on the
// way. RoomBot runs against a real MemoryChatStore and a scripted HTTP
// transport under the real microgpt client, so the prompt is checked as
// the JSON microgpt-serve would receive.

#include "domains/games/apis/games_hub/room_bot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "domains/ai/libs/microgpt_cpp/client.h"
#include "domains/games/apis/games_hub/chat_store.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace games_hub {
namespace {

using moonbase::microgpt::Message;

// microgpt-serve, scripted: each request takes the next answer, or is
// held until Release() when Hold() was called first.
class FakeMicrogpt final : public opal::http::HttpClient {
 public:
  struct Answer {
    int status = 200;
    std::string content;
    bool transport_error = false;
  };

  void Push(Answer answer) {
    const std::lock_guard<std::mutex> lock(mu_);
    answers_.push_back(std::move(answer));
  }
  void Hold() {
    const std::lock_guard<std::mutex> lock(mu_);
    held_ = true;
  }
  void Release() {
    const std::lock_guard<std::mutex> lock(mu_);
    held_ = false;
    cv_.notify_all();
  }
  bool AwaitRequests(std::size_t n) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, std::chrono::seconds(5), [&] { return bodies_.size() >= n; });
  }
  std::vector<std::string> bodies() {
    const std::lock_guard<std::mutex> lock(mu_);
    return bodies_;
  }

  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    std::unique_lock<std::mutex> lock(mu_);
    bodies_.push_back(request.body);
    cv_.notify_all();
    cv_.wait(lock, [&] { return !held_; });
    if (answers_.empty()) return opal::Error::Transport("no answer scripted", false);
    Answer answer = answers_.front();
    answers_.pop_front();
    if (answer.transport_error) return opal::Error::Transport("timed out", false);
    opal::http::HttpResponse response;
    response.status = answer.status;
    response.body =
        nlohmann::json{{"role", "assistant"}, {"content", answer.content}, {"tokens_dropped", 0}}
            .dump();
    return response;
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Answer> answers_;
  std::vector<std::string> bodies_;
  bool held_ = false;
};

// ---- the pure pieces ----

TEST(BotMention, IsAPrefixMentionCaseInsensitivelyAndStripsIt) {
  EXPECT_EQ(BotMention("@bot hi there"), std::optional<std::string>("hi there"));
  EXPECT_EQ(BotMention("@BoT   who wins?"), std::optional<std::string>("who wins?"));
  EXPECT_EQ(BotMention("@bot"), std::optional<std::string>(""));
  EXPECT_EQ(BotMention("@bot\thi"), std::optional<std::string>("hi"));
  EXPECT_EQ(BotMention("hello @bot"), std::nullopt);
  EXPECT_EQ(BotMention("@botany is fun"), std::nullopt);
  EXPECT_EQ(BotMention(" @bot hi"), std::nullopt);
}

TEST(TruncateUtf8, CutsAtTheLimitWithoutSplittingACharacter) {
  EXPECT_EQ(TruncateUtf8("hello", 10), "hello");
  EXPECT_EQ(TruncateUtf8("hello", 3), "hel");
  // "é" is two bytes: a cut through it drops the whole character.
  EXPECT_EQ(TruncateUtf8("aé", 2), "a");
  EXPECT_EQ(TruncateUtf8("aé", 3), "aé");
  // "😀" is four bytes.
  EXPECT_EQ(TruncateUtf8("ab😀", 5), "ab");
}

ChatRow Row(int64_t id, std::string player, std::string text) {
  return ChatRow{
      .message_id = id, .room_id = "R1", .player_id = std::move(player), .text = std::move(text)};
}

TEST(BotPrompt, MapsRolesStripsMentionsAndEndsAtTheTrigger) {
  const std::vector<ChatRow> rows = {
      Row(1, "alice", "hi all"),
      Row(2, "bob", "@bot who wins?"),
      Row(3, kBotPlayerId, "whoever knocks last"),
      Row(4, "alice", "@bot and then?"),
      Row(5, "bob", "said after the trigger"),
  };
  const std::vector<Message> prompt = BotPrompt(rows, /*trigger_id=*/4);

  ASSERT_EQ(prompt.size(), 4u);
  EXPECT_EQ(prompt[0].role, "user");
  EXPECT_EQ(prompt[0].content, "alice: hi all");
  EXPECT_EQ(prompt[1].content, "bob: who wins?");
  EXPECT_EQ(prompt[2].role, "assistant");
  EXPECT_EQ(prompt[2].content, "whoever knocks last");
  EXPECT_EQ(prompt[3].role, "user");
  EXPECT_EQ(prompt[3].content, "alice: and then?");
}

TEST(BotPrompt, KeepsTheNewestMessagesWithinCountAndBytes) {
  std::vector<ChatRow> rows;
  for (int i = 1; i <= 12; ++i) rows.push_back(Row(i, "alice", "message " + std::to_string(i)));
  auto prompt = BotPrompt(rows, 12);
  ASSERT_EQ(prompt.size(), kBotPromptMessages);
  EXPECT_EQ(prompt.front().content, "alice: message 5");
  EXPECT_EQ(prompt.back().content, "alice: message 12");

  // Long history is trimmed oldest first; the trigger always stays.
  rows = {Row(1, "alice", std::string(400, 'a')), Row(2, "alice", std::string(400, 'b')),
          Row(3, "alice", std::string(400, 'c')), Row(4, "alice", std::string(400, 'd')),
          Row(5, "alice", "@bot " + std::string(450, 'e'))};
  prompt = BotPrompt(rows, 5);
  std::size_t bytes = 0;
  for (const auto& message : prompt) bytes += message.content.size();
  EXPECT_LE(bytes, kBotPromptBytes);
  ASSERT_FALSE(prompt.empty());
  EXPECT_EQ(prompt.back().content, "alice: " + std::string(450, 'e'));
  EXPECT_LT(prompt.size(), 5u);
}

// ---- the bot against a store and a scripted microgpt ----

class RoomBotTest : public ::testing::Test {
 protected:
  RoomBotTest()
      : store_(std::make_shared<MemoryChatStore>([this](const std::string& room_id,
                                                        const std::string& player_id,
                                                        const MemberAction& action) {
          if (!members_.contains({room_id, player_id})) return false;
          action();
          return true;
        })),
        microgpt_(std::make_shared<FakeMicrogpt>()),
        metrics_(std::make_shared<futility::otel::CapturingMetricsRecorder>()) {
    members_ = {{"R1", "alice"}, {"R1", "bob"}, {"R2", "carol"}};
  }

  void Start(BotLimits limits = {}) {
    opal::ClientConfig config = microgpt::DefaultClientConfig("http://microgpt-serve:8087");
    config.http_client = microgpt_;
    auto client = microgpt::Client::Create(std::move(config));
    ASSERT_TRUE(client.ok());
    bot_ = std::make_unique<RoomBot>(
        std::make_shared<microgpt::Client>(std::move(*client)), store_,
        [this](const std::string& room_id) {
          const std::lock_guard<std::mutex> lock(posted_mu_);
          posted_.push_back(room_id);
        },
        metrics_, "instance-a", limits);
  }

  // Appends as a player would and hands the committed row to the bot.
  ChatRow Say(const std::string& room_id, const std::string& player_id, const std::string& text) {
    auto row = store_->Append(room_id, player_id, text, "instance-a");
    EXPECT_TRUE(row.ok()) << row.status();
    bot_->OnMessage(*row);
    return *row;
  }

  std::vector<ChatRow> History(const std::string& room_id) {
    auto rows = store_->LoadRecent(room_id, kChatHistoryLimit);
    EXPECT_TRUE(rows.ok());
    return rows.ok() ? *rows : std::vector<ChatRow>{};
  }

  double Results(const std::string& result) {
    return metrics_->CounterTotal("bot_requests", {{"result", result}});
  }

  std::set<std::pair<std::string, std::string>> members_;
  std::shared_ptr<MemoryChatStore> store_;
  std::shared_ptr<FakeMicrogpt> microgpt_;
  std::shared_ptr<futility::otel::CapturingMetricsRecorder> metrics_;
  std::mutex posted_mu_;
  std::vector<std::string> posted_;
  std::unique_ptr<RoomBot> bot_;
};

TEST_F(RoomBotTest, AMentionGetsAReplyPostedAsTheBotAfterTheAsker) {
  Start();
  microgpt_->Push({.content = "whoever knocks last"});
  const ChatRow asked = Say("R1", "alice", "@bot who wins?");
  bot_->Drain();

  const auto history = History("R1");
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].message_id, asked.message_id);
  EXPECT_EQ(history[1].player_id, kBotPlayerId);
  EXPECT_EQ(history[1].text, "whoever knocks last");
  EXPECT_EQ(posted_, std::vector<std::string>{"R1"});
  EXPECT_EQ(Results("ok"), 1);

  const auto body = nlohmann::json::parse(microgpt_->bodies().at(0));
  EXPECT_EQ(body["max_tokens"], kBotMaxTokens);
  ASSERT_EQ(body["messages"].size(), 1u);
  EXPECT_EQ(body["messages"][0]["content"], "alice: who wins?");
}

TEST_F(RoomBotTest, OnlyAPrefixMentionAsks) {
  Start();
  Say("R1", "alice", "hello @bot");
  Say("R1", "alice", "@botany");
  bot_->Drain();
  EXPECT_TRUE(microgpt_->bodies().empty());
  EXPECT_EQ(History("R1").size(), 2u);
}

TEST_F(RoomBotTest, AMentionWhileTheRoomsRequestIsInFlightIsDropped) {
  Start();
  microgpt_->Hold();
  microgpt_->Push({.content = "first"});
  Say("R1", "alice", "@bot one");
  ASSERT_TRUE(microgpt_->AwaitRequests(1));
  Say("R1", "bob", "@bot two");
  // Another room's mention is not dropped: it waits its turn.
  microgpt_->Push({.content = "for carol"});
  Say("R2", "carol", "@bot three");
  microgpt_->Release();
  bot_->Drain();

  EXPECT_EQ(Results("busy"), 1);
  EXPECT_EQ(microgpt_->bodies().size(), 2u);
  EXPECT_EQ(History("R1").back().text, "first");
  EXPECT_EQ(History("R2").back().text, "for carol");
}

TEST_F(RoomBotTest, TheRoomsBudgetRefusesPastItsBurst) {
  Start(BotLimits{.room_burst = 2, .room_refill_per_sec = 0});
  for (int i = 0; i < 3; ++i) {
    microgpt_->Push({.content = "reply"});
    Say("R1", "alice", "@bot again");
    bot_->Drain();
  }
  EXPECT_EQ(Results("ok"), 2);
  EXPECT_EQ(Results("rate_limited"), 1);
  EXPECT_EQ(microgpt_->bodies().size(), 2u);
  // Another room has its own budget.
  microgpt_->Push({.content = "reply"});
  Say("R2", "carol", "@bot hi");
  bot_->Drain();
  EXPECT_EQ(Results("ok"), 3);
}

TEST_F(RoomBotTest, TheHubsBudgetBoundsEveryRoomTogether) {
  Start(BotLimits{.hub_burst = 1, .hub_refill_per_sec = 0});
  microgpt_->Push({.content = "reply"});
  Say("R1", "alice", "@bot hi");
  bot_->Drain();
  Say("R2", "carol", "@bot hi");
  bot_->Drain();
  EXPECT_EQ(Results("ok"), 1);
  EXPECT_EQ(Results("rate_limited"), 1);
}

TEST_F(RoomBotTest, ALongReplyIsCutAtTheByteLimitOnACharacterBoundary) {
  Start();
  // 499 ASCII bytes then a two-byte character straddling byte 500.
  microgpt_->Push({.content = std::string(499, 'x') + "é and more"});
  Say("R1", "alice", "@bot go on");
  bot_->Drain();

  const std::string reply = History("R1").back().text;
  EXPECT_EQ(reply, std::string(499, 'x'));
}

TEST_F(RoomBotTest, FailuresAndEmptyRepliesPostNothing) {
  Start(BotLimits{.room_burst = 10});
  microgpt_->Push({.status = 200, .content = "", .transport_error = true});
  Say("R1", "alice", "@bot one");
  bot_->Drain();
  microgpt_->Push({.status = 429, .content = ""});
  Say("R1", "alice", "@bot two");
  bot_->Drain();
  microgpt_->Push({.content = "   "});
  Say("R1", "alice", "@bot three");
  bot_->Drain();

  EXPECT_EQ(Results("unreachable"), 1);
  EXPECT_EQ(Results("error"), 1);
  EXPECT_EQ(Results("empty"), 1);
  for (const ChatRow& row : History("R1")) EXPECT_NE(row.player_id, kBotPlayerId);
  EXPECT_TRUE(posted_.empty());
}

// The asker's membership authorizes the reply: an asker who left while
// microgpt was thinking gets no reply posted in their name.
TEST_F(RoomBotTest, AnAskerWhoLeftGetsNoReply) {
  Start();
  microgpt_->Hold();
  microgpt_->Push({.content = "too late"});
  Say("R1", "alice", "@bot hi");
  ASSERT_TRUE(microgpt_->AwaitRequests(1));
  members_.erase({"R1", "alice"});
  microgpt_->Release();
  bot_->Drain();

  EXPECT_EQ(Results("error"), 1);
  EXPECT_EQ(History("R1").size(), 1u);
}

}  // namespace
}  // namespace games_hub
