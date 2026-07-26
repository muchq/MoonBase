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

/// The durable ChatStore (#1226). Every append is one CTE-chained
/// statement — the same idiom PgHubStore uses for game commits — so the
/// membership check, the insert, the prune, and the NOTIFY either all
/// land or none do without an explicit transaction.
///
/// The statement takes a FOR UPDATE lock on the room row, which is what
/// makes cursor catch-up sound: appends to one room serialize, so
/// message_id order is also commit order and a reader that has seen id N
/// can never be shown an id below N afterwards. Without it two appends
/// racing in the same room could commit out of sequence and a reader
/// polling LoadAfter would step over the slower one. Rooms don't
/// contend with each other, only with their own deletion.
///
/// Appends run through pg::Client::ExecOnce, so a connection lost
/// mid-statement is reported instead of retried: a chat line duplicated
/// under a second id is worse than one reported as failed.
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
