#include "domains/games/apis/golf_hub/chat_store.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace golf_hub {
namespace {

/// Whether every byte belongs to a well-formed UTF-8 scalar. Overlong
/// encodings, surrogates, and anything past U+10FFFF are rejected: they
/// round-trip through some decoders and not others, and a `text` column
/// will not take them at all.
bool IsStorableUtf8(const std::string& text) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char lead = bytes[i];
    std::size_t continuations = 0;
    char32_t code = 0;
    if (lead < 0x80) {
      ++i;
      continue;
    } else if ((lead & 0xE0) == 0xC0) {
      continuations = 1;
      code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      continuations = 2;
      code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      continuations = 3;
      code = lead & 0x07;
    } else {
      return false;
    }
    if (i + continuations >= text.size()) return false;
    for (std::size_t j = 1; j <= continuations; ++j) {
      const unsigned char continuation = bytes[i + j];
      if ((continuation & 0xC0) != 0x80) return false;
      code = (code << 6) | (continuation & 0x3F);
    }
    static constexpr char32_t kSmallest[] = {0, 0x80, 0x800, 0x10000};
    if (code < kSmallest[continuations]) return false;
    if (code > 0x10FFFF) return false;
    if (code >= 0xD800 && code <= 0xDFFF) return false;
    i += continuations + 1;
  }
  return true;
}

}  // namespace

absl::Status ValidateChatText(const std::string& text) {
  if (text.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
    return absl::InvalidArgumentError("chat text is empty");
  }
  if (text.size() > kChatTextByteLimit) {
    return absl::InvalidArgumentError("chat text is too long");
  }
  if (text.find('\0') != std::string::npos) {
    return absl::InvalidArgumentError("chat text contains a NUL byte");
  }
  if (!IsStorableUtf8(text)) {
    return absl::InvalidArgumentError("chat text is not valid UTF-8");
  }
  return absl::OkStatus();
}

absl::Status NotAMemberError() {
  return absl::FailedPreconditionError("sender is not a member of the room");
}

MemoryChatStore::MemoryChatStore(MemberCheck is_member) : is_member_(std::move(is_member)) {}

absl::StatusOr<ChatRow> MemoryChatStore::Append(const std::string& room_id,
                                                const std::string& player_id,
                                                const std::string& text,
                                                const std::string& /*notify_payload*/) {
  if (const absl::Status valid = ValidateChatText(text); !valid.ok()) return valid;
  // Outside mu_ on purpose: the check reaches into whatever owns room
  // state, which has its own lock.
  if (!is_member_(room_id, player_id)) return NotAMemberError();

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
