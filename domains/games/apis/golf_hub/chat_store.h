#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace golf_hub {

inline constexpr std::size_t kChatHistoryLimit = 100;
inline constexpr std::size_t kChatTextByteLimit = 500;
inline std::string ChatChannel(const std::string& room_id) { return "chat_" + room_id; }

/// Rejects empty/whitespace-only text and text over kChatTextByteLimit bytes.
/// Handlers validate before appending so clients get a protocol-level
/// rejection; stores call it again so retention stays bounded regardless.
absl::Status ValidateChatText(const std::string& text);

/// One committed message. message_id is the only ordering key: timestamps
/// come from the wall clock and can move backwards across an adjustment, so
/// sent_at_unix_millis is for display, never for sorting or deduplication.
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
/// Rooms retain their newest kChatHistoryLimit messages and nothing older.
class ChatStore {
 public:
  virtual ~ChatStore() = default;

  /// Commits one message and returns the row that readers will observe.
  virtual absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                         const std::string& text,
                                         const std::string& notify_payload) = 0;

  /// The newest `limit` retained rows, ascending by message_id.
  virtual absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                          std::size_t limit) = 0;

  /// Retained rows above `after_message_id`, ascending, at most `limit`.
  /// Retention prunes rows a lagging cursor never read: this returns the
  /// oldest row still retained above the cursor and does not report the gap,
  /// so catch-up is bounded by kChatHistoryLimit rather than by the cursor.
  virtual absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                         int64_t after_message_id,
                                                         std::size_t limit) = 0;

  /// Erases a room's history. MemoryChatStore reclaims memory only here, so
  /// handlers must call this whenever a room is deleted; PostgreSQL
  /// implementations cascade from the room row and no-op.
  virtual void DropRoom(const std::string& room_id) = 0;
};

/// Process-local chat persistence for non-PostgreSQL production and tests.
/// The handler authorizes appends; this class owns ordering and retention.
/// History does not survive a restart and does not reach other instances,
/// and rooms accumulate until DropRoom erases them.
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
