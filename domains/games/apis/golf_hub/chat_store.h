#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_CHAT_STORE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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

/// Rejects text a room cannot store: empty or whitespace-only, over
/// kChatTextByteLimit *bytes*, or not storable UTF-8. The limit counts
/// bytes rather than characters, and text that would split a character
/// is rejected rather than truncated. Embedded NULs are rejected too:
/// libpq sends text parameters as C strings, so a NUL would silently
/// truncate the message on its way to the server.
///
/// Handlers validate before appending so clients get a protocol-level
/// rejection; stores call it again so retention stays bounded regardless.
absl::Status ValidateChatText(const std::string& text);

/// The rejection every store returns when the sender is not a current
/// member of the room — including when the room itself is gone. Callers
/// tell it apart from a database failure to distinguish a stale send
/// from an outage, so both implementations must return the same thing.
absl::Status NotAMemberError();

/// Answers whether a player is a current member of a room. Chat appends
/// are authorized through this, so a store enforces membership without
/// knowing how rooms are stored.
using MemberCheck = std::function<bool(const std::string& room_id, const std::string& player_id)>;

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

/// Authoritative, ordered room-chat persistence. Rooms retain their
/// newest kChatHistoryLimit messages and nothing older.
///
/// Appends are authorized against room membership here, not only in the
/// handler, so a membership row that vanishes while a send is in flight
/// rejects the message instead of storing it. Where that check is atomic
/// with the insert differs by implementation; that it happens does not.
class ChatStore {
 public:
  virtual ~ChatStore() = default;

  /// Commits one message and returns the row that readers will observe.
  /// NotAMemberError means the sender is not in the room (or the room is
  /// gone); other failures mean the store could not be reached.
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
/// History does not survive a restart and does not reach other instances,
/// and rooms accumulate until DropRoom erases them.
///
/// Membership comes from the injected check, which runs before the
/// store's own lock is taken — so the room state it consults may use a
/// lock of its own without an ordering hazard. That leaves a window
/// between the check and the insert; single-instance mode closes it the
/// same way it always has, by re-checking under the handler's lock
/// before delivering. PostgreSQL closes it with a row lock instead.
class MemoryChatStore final : public ChatStore {
 public:
  /// There is no default: a store that cannot answer "is this player in
  /// this room?" would accept messages nobody is authorized to send.
  explicit MemoryChatStore(MemberCheck is_member);

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
  const MemberCheck is_member_;
  std::mutex mu_;
  std::map<std::string, std::deque<ChatRow>> chats_;
  int64_t next_message_id_ = 1;
};

}  // namespace golf_hub

#endif
