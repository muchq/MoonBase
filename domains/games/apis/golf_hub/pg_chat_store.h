#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_PG_CHAT_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_PG_CHAT_STORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

/// The durable ChatStore (#1226). An append locks the room, inserts,
/// prunes to the retention window, and notifies, all in one
/// transaction, so either every part lands or none does.
///
/// Two properties come out of that lock, and they need different
/// mechanisms. Ordering comes from the lock itself: appends to a room
/// serialize from lock to commit, so message_id order is also commit
/// order and a cursor that has seen id N is never later shown an id
/// below it. Retention comes from the statement split: a query takes
/// its snapshot before it waits on a lock, so a single CTE-chained
/// statement would prune against a view of the room from before the
/// previous append committed and leave the room one row over the
/// window. Statements issued after the lock is held take fresh
/// snapshots and see everything committed ahead of them.
///
/// Nothing is retried. pg::Client heals a dropped connection by
/// re-running a statement, which is safe for a conditional write but
/// not for an append: the second run would post the same message again
/// under a new id, and no consumer can dedupe that.
class PgChatStore final : public ChatStore {
 public:
  explicit PgChatStore(std::shared_ptr<pg::Client> db);
  PgChatStore(const PgChatStore&) = delete;
  PgChatStore& operator=(const PgChatStore&) = delete;

  /// Commits one message and notifies ChatChannel(room_id) with
  /// notify_payload in the same statement. FailedPrecondition means the
  /// room is gone or the sender is not one of its members — the check
  /// is the durable room_members row, not the caller's word.
  absl::StatusOr<ChatRow> Append(const std::string& room_id, const std::string& player_id,
                                 const std::string& text,
                                 const std::string& notify_payload) override;

  absl::StatusOr<std::vector<ChatRow>> LoadRecent(const std::string& room_id,
                                                  std::size_t limit) override;
  absl::StatusOr<std::vector<ChatRow>> LoadAfter(const std::string& room_id,
                                                 int64_t after_message_id,
                                                 std::size_t limit) override;

  /// Nothing to do: room_chat_messages cascades from the room row, so
  /// the hub's existing DeleteRoom write already erased this history.
  void DropRoom(const std::string& room_id) override;

 private:
  const std::shared_ptr<pg::Client> db_;
};

}  // namespace golf_hub

#endif
