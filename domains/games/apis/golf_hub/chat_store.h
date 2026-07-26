#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace golf_hub {

inline constexpr std::size_t kChatHistoryLimit = 100;
inline std::string ChatChannel(const std::string& room_id) { return "golf_chat_" + room_id; }

struct ChatRow {
  int64_t message_id = 0;
  std::string room_id;
  std::string player_id;
  std::string text;
  int64_t sent_at_unix_millis = 0;
};

/// Authoritative, ordered room-chat persistence. Room membership remains
/// HubHandler/HubStore's responsibility; PostgreSQL implementations also
/// guard appends against the durable membership row in their transaction.
class ChatStore {
 public:
  virtual ~ChatStore() = default;

  virtual absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                         const std::string& text,
                                         const std::string& notify_payload) = 0;
  virtual absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                          std::size_t limit) = 0;
  virtual absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                         int64_t after_message_id,
                                                         std::size_t limit) = 0;
  virtual void DropRoom(const std::string& room_id) = 0;
};

/// Process-local chat persistence for non-PostgreSQL production and tests.
/// The handler authorizes appends; this class owns ordering and retention.
class MemoryChatStore final : public ChatStore {
 public:
  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override;
  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override;
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override;
  void DropRoom(const std::string& room_id) override;

 private:
  std::mutex mu_;
  std::map<std::string, std::deque<ChatRow>> chats_;
  int64_t next_message_id_ = 1;
};

}  // namespace golf_hub

#endif
