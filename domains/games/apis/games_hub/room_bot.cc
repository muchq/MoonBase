#include "domains/games/apis/games_hub/room_bot.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "opal/core/error.h"

namespace games_hub {
namespace {

constexpr std::string_view kMention = "@bot";

}  // namespace

std::optional<std::string> BotMention(std::string_view text) {
  if (text.size() < kMention.size()) return std::nullopt;
  if (!absl::EqualsIgnoreCase(text.substr(0, kMention.size()), kMention)) return std::nullopt;
  std::string_view rest = text.substr(kMention.size());
  if (!rest.empty() && !absl::ascii_isspace(static_cast<unsigned char>(rest.front()))) {
    return std::nullopt;
  }
  return std::string(absl::StripAsciiWhitespace(rest));
}

std::string TruncateUtf8(std::string text, std::size_t max_bytes) {
  if (text.size() <= max_bytes) return text;
  std::size_t cut = max_bytes;
  // Back off continuation bytes to the start of the character the cut
  // lands in, and cut before it.
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  text.resize(cut);
  return text;
}

std::vector<moonbase::microgpt::Message> BotPrompt(const std::vector<ChatRow>& rows,
                                                   int64_t trigger_id) {
  std::vector<moonbase::microgpt::Message> prompt;
  for (const ChatRow& row : rows) {
    if (row.message_id > trigger_id) continue;
    if (row.player_id == kBotPlayerId) {
      prompt.push_back({.role = "assistant", .content = row.text});
      continue;
    }
    const std::optional<std::string> question = BotMention(row.text);
    prompt.push_back(
        {.role = "user", .content = row.player_id + ": " + question.value_or(row.text)});
  }
  if (prompt.size() > kBotPromptMessages) {
    prompt.erase(prompt.begin(), prompt.end() - kBotPromptMessages);
  }
  std::size_t bytes = 0;
  for (const auto& message : prompt) bytes += message.content.size();
  // Oldest first, never the trigger itself.
  std::size_t drop = 0;
  while (bytes > kBotPromptBytes && drop + 1 < prompt.size()) {
    bytes -= prompt[drop].content.size();
    ++drop;
  }
  prompt.erase(prompt.begin(), prompt.begin() + static_cast<std::ptrdiff_t>(drop));
  return prompt;
}

RoomBot::RoomBot(std::shared_ptr<microgpt::Client> client, std::shared_ptr<ChatStore> store,
                 Posted posted, std::shared_ptr<futility::otel::MetricsRecorder> metrics,
                 std::string notify_payload, BotLimits limits)
    : client_(std::move(client)),
      store_(std::move(store)),
      posted_(std::move(posted)),
      metrics_(std::move(metrics)),
      notify_payload_(std::move(notify_payload)),
      limits_(limits),
      hub_budget_(limits.hub_burst, limits.hub_refill_per_sec),
      worker_([this] { WorkerMain(); }) {}

RoomBot::~RoomBot() {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  worker_.join();
}

void RoomBot::OnMessage(const ChatRow& row) {
  if (row.player_id == kBotPlayerId || !BotMention(row.text).has_value()) return;
  const char* refused = nullptr;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    PruneBudgetsLocked(now);
    if (in_flight_.contains(row.room_id) || queue_.size() >= limits_.queue) {
      refused = "busy";
    } else {
      auto budget = room_budgets_.find(row.room_id);
      if (budget == room_budgets_.end()) {
        budget =
            room_budgets_
                .emplace(
                    row.room_id,
                    RoomBudget{TokenBucket(limits_.room_burst, limits_.room_refill_per_sec), now})
                .first;
      }
      budget->second.last_used = now;
      // The room's budget first, so one room's burst never spends the
      // hub's tokens on requests it was going to refuse anyway.
      if (!budget->second.bucket.Admit(now) || !hub_budget_.Admit(now)) {
        refused = "rate_limited";
      } else {
        in_flight_.insert(row.room_id);
        queue_.push_back({row.room_id, row.player_id, row.message_id});
      }
    }
  }
  if (refused != nullptr) {
    Count(refused);
    return;
  }
  cv_.notify_all();
}

void RoomBot::Drain() {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait(lock, [this] { return queue_.empty() && in_flight_.empty(); });
}

void RoomBot::WorkerMain() {
  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    Answer(job);
    {
      const std::lock_guard<std::mutex> lock(mu_);
      in_flight_.erase(job.room_id);
    }
    cv_.notify_all();
  }
}

void RoomBot::Answer(const Job& job) {
  auto rows = store_->LoadRecent(job.room_id, kChatHistoryLimit);
  if (!rows.ok()) {
    LOG(WARNING) << "room bot history load failed: " << rows.status();
    Count("error");
    return;
  }
  const auto started = std::chrono::steady_clock::now();
  auto reply = client_->Chat(BotPrompt(*rows, job.trigger_id), kBotMaxTokens);
  if (metrics_ != nullptr) {
    metrics_->RecordLatency("bot_latency_us", std::chrono::duration_cast<std::chrono::microseconds>(
                                                  std::chrono::steady_clock::now() - started));
  }
  if (!reply.ok()) {
    // A timeout and a refused connection look alike from here: the
    // transport kind is everything short of an answer.
    const bool unreachable = reply.error().kind() == opal::ErrorKind::kTransport;
    Count(unreachable ? "unreachable" : "error");
    return;
  }
  const std::string text = TruncateUtf8(reply->content, kChatTextByteLimit);
  if (absl::StripAsciiWhitespace(text).empty()) {
    Count("empty");
    return;
  }
  // The same rule as any sender's text: a reply the store would refuse
  // (a NUL, say) is dropped here rather than half-stored.
  if (!ValidateChatText(text).ok()) {
    Count("error");
    return;
  }
  auto appended = store_->AppendAs(job.room_id, job.asker_id, kBotPlayerId, text, notify_payload_);
  if (!appended.ok()) {
    // Mostly an asker who left while microgpt was thinking.
    Count("error");
    return;
  }
  Count("ok");
  posted_(job.room_id);
}

void RoomBot::Count(const char* result) {
  if (metrics_ != nullptr) metrics_->RecordCounter("bot_requests", 1, {{"result", result}});
}

void RoomBot::PruneBudgetsLocked(std::chrono::steady_clock::time_point now) {
  if (limits_.room_refill_per_sec <= 0) return;
  const auto full_after = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(limits_.room_burst / limits_.room_refill_per_sec));
  std::erase_if(room_budgets_,
                [&](const auto& entry) { return now - entry.second.last_used > full_after; });
}

}  // namespace games_hub
