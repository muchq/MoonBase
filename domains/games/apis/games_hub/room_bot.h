#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ROOM_BOT_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ROOM_BOT_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "domains/ai/libs/microgpt_cpp/client.h"
#include "domains/games/apis/games_hub/chat_store.h"
#include "domains/games/apis/games_hub/rate_limiter.h"
#include "domains/platform/libs/futility/otel/metrics.h"

namespace games_hub {

/// How much room history a prompt carries, newest first: at most this
/// many messages and this many content bytes, the trigger always kept.
inline constexpr std::size_t kBotPromptMessages = 8;
inline constexpr std::size_t kBotPromptBytes = 1500;
/// Generation is synchronous and CPU-bound on a half-core service, so a
/// reply is kept to a line.
inline constexpr int kBotMaxTokens = 60;

/// The question in a mention: text that starts "@bot" (any case) followed
/// by whitespace or nothing, with that prefix and the whitespace after it
/// stripped. Nothing for text that does not start with the mention.
std::optional<std::string> BotMention(std::string_view text);

/// `text` cut to at most `max_bytes` without splitting a UTF-8 character.
std::string TruncateUtf8(std::string text, std::size_t max_bytes);

/// The conversation microgpt is asked to continue: the room's rows up to
/// and including `trigger_id`, oldest first, trimmed to the newest
/// kBotPromptMessages and kBotPromptBytes. A player's message is a "user"
/// turn prefixed with their id and stripped of any mention; the bot's own
/// replies are "assistant" turns.
std::vector<moonbase::microgpt::Message> BotPrompt(const std::vector<ChatRow>& rows,
                                                   int64_t trigger_id);

/// Budgets for the bot's calls. A room's bucket keeps one conversation from
/// monopolizing the bot; the hub's keeps every room together under
/// microgpt-serve's per-IP limit (5 requests a second), which counts this
/// whole instance as one client.
struct BotLimits {
  double room_burst = 2;
  double room_refill_per_sec = 0.1;
  double hub_burst = 4;
  double hub_refill_per_sec = 4;
  std::size_t queue = 8;
};

/// Answers "@bot" mentions in room chat with microgpt (#1591). One worker
/// thread makes every call, so no stream or the hub's lock ever waits on
/// microgpt; a room has at most one request in flight, and a mention that
/// arrives meanwhile is dropped rather than queued behind it. A reply is
/// appended as kBotPlayerId on the asker's membership and then handed to
/// `posted` for local delivery; the store's NOTIFY reaches other instances.
///
/// Best effort throughout: a refusal, a failure or an empty reply is
/// counted in bot_requests and posts nothing.
class RoomBot {
 public:
  using Posted = std::function<void(const std::string& room_id)>;

  RoomBot(std::shared_ptr<microgpt::Client> client, std::shared_ptr<ChatStore> store, Posted posted,
          std::shared_ptr<futility::otel::MetricsRecorder> metrics, std::string notify_payload,
          BotLimits limits = {});
  /// Finishes the call in flight, if any, and drops the rest.
  ~RoomBot();

  RoomBot(const RoomBot&) = delete;
  RoomBot& operator=(const RoomBot&) = delete;

  /// Considers a message that has just committed. Returns at once; the
  /// call to microgpt, if any, happens on the worker.
  void OnMessage(const ChatRow& row);

  /// Blocks until nothing is queued or in flight. For tests.
  void Drain();

 private:
  struct Job {
    std::string room_id;
    std::string asker_id;
    int64_t trigger_id = 0;
  };
  struct RoomBudget {
    TokenBucket bucket;
    std::chrono::steady_clock::time_point last_used;
  };

  void WorkerMain();
  void Answer(const Job& job);
  void Count(const char* result);
  // Forgets buckets idle long enough to have refilled: dropping one is the
  // same as keeping it full, and rooms come and go.
  void PruneBudgetsLocked(std::chrono::steady_clock::time_point now);

  const std::shared_ptr<microgpt::Client> client_;
  const std::shared_ptr<ChatStore> store_;
  const Posted posted_;
  const std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const std::string notify_payload_;
  const BotLimits limits_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
  std::set<std::string> in_flight_;
  std::map<std::string, RoomBudget> room_budgets_;
  TokenBucket hub_budget_;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace games_hub

#endif
