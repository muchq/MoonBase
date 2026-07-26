#include "domains/games/apis/golf_hub/chat_store.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace golf_hub {

absl::StatusOr<ChatRow> MemoryChatStore::Append(const std::string& room_id,
                                                const std::string& player_id,
                                                const std::string& text,
                                                const std::string& /*notify_payload*/) {
  const std::lock_guard<std::mutex> lock(mu_);
  ChatRow row;
  row.message_id = next_message_id_++;
  row.room_id = room_id;
  row.player_id = player_id;
  row.text = text;
  row.sent_at_unix_millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();

  auto& chat = chats_[room_id];
  chat.push_back(row);
  while (chat.size() > kChatHistoryLimit) chat.pop_front();
  return row;
}

absl::StatusOr<std::vector<ChatRow>> MemoryChatStore::LoadRecent(const std::string& room_id,
                                                                 std::size_t limit) {
  const std::lock_guard<std::mutex> lock(mu_);
  std::vector<ChatRow> rows;
  const auto chat = chats_.find(room_id);
  if (chat == chats_.end() || limit == 0) return rows;
  const std::size_t start = chat->second.size() - std::min(limit, chat->second.size());
  rows.insert(rows.end(), chat->second.begin() + static_cast<std::ptrdiff_t>(start),
              chat->second.end());
  return rows;
}

absl::StatusOr<std::vector<ChatRow>> MemoryChatStore::LoadAfter(const std::string& room_id,
                                                                int64_t after_message_id,
                                                                std::size_t limit) {
  const std::lock_guard<std::mutex> lock(mu_);
  std::vector<ChatRow> rows;
  const auto chat = chats_.find(room_id);
  if (chat == chats_.end() || limit == 0) return rows;
  for (const ChatRow& row : chat->second) {
    if (row.message_id <= after_message_id) continue;
    rows.push_back(row);
    if (rows.size() == limit) break;
  }
  return rows;
}

void MemoryChatStore::DropRoom(const std::string& room_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  chats_.erase(room_id);
}

}  // namespace golf_hub
