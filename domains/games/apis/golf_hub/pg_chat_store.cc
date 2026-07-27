#include "domains/games/apis/golf_hub/pg_chat_store.h"

#include <cstdlib>
#include <optional>
#include <utility>

#include "absl/status/status.h"

namespace golf_hub {
namespace {

// Append is four statements in one transaction, and the split is the
// point rather than an accident. A CTE-chained single statement takes
// one snapshot before it blocks on the room lock, so a concurrent
// append that commits while it waits stays invisible to its prune and
// the room keeps 101 rows. Separate statements after the lock each take
// a fresh snapshot, so the prune sees every committed message.

// Locks the room and authorizes in one round trip. FOR UPDATE OF r
// locks only the room row: appends to a room serialize from here until
// commit, which is what makes message_id order match commit order, so a
// cursor that has seen id N is never later shown something below it.
// Membership is read from the durable row, not taken on the caller's
// word, and a non-member simply finds nothing to lock.
constexpr char kLockRoomForMember[] = R"sql(
    SELECT 1 FROM rooms r
    JOIN room_members m ON m.room_id = r.room_id
    WHERE r.room_id = $1 AND m.player_id = $2
    FOR UPDATE OF r)sql";

constexpr char kInsert[] = R"sql(
    INSERT INTO room_chat_messages (room_id, player_id, body) VALUES ($1, $2, $3)
    RETURNING message_id, (EXTRACT(EPOCH FROM sent_at) * 1000)::bigint)sql";

// Trims to the window and wakes listeners together. The row inserted
// above is visible here — same transaction, later command — so the
// window counts it, unlike the single-statement version this replaces.
constexpr char kPruneAndNotify[] = R"sql(
    WITH pruned AS (
      DELETE FROM room_chat_messages c
      WHERE c.room_id = $1
        AND c.message_id < (
          SELECT min(message_id) FROM (
            SELECT message_id FROM room_chat_messages
            WHERE room_id = $1 ORDER BY message_id DESC LIMIT $2::bigint) keep))
    SELECT pg_notify($3, $4))sql";

constexpr char kLoadRecent[] = R"sql(
    SELECT message_id, player_id, body, (EXTRACT(EPOCH FROM sent_at) * 1000)::bigint
    FROM (
      SELECT message_id, player_id, body, sent_at FROM room_chat_messages
      WHERE room_id = $1 ORDER BY message_id DESC LIMIT $2::bigint) recent
    ORDER BY message_id)sql";

constexpr char kLoadAfter[] = R"sql(
    SELECT message_id, player_id, body, (EXTRACT(EPOCH FROM sent_at) * 1000)::bigint
    FROM room_chat_messages
    WHERE room_id = $1 AND message_id > $2::bigint
    ORDER BY message_id LIMIT $3::bigint)sql";

int64_t ToInt64(const std::optional<std::string>& value) {
  return std::atoll(value.value_or("0").c_str());
}

absl::StatusOr<std::vector<ChatRow>> ReadRows(absl::StatusOr<pg::Result> result,
                                              const std::string& room_id) {
  if (!result.ok()) return result.status();
  std::vector<ChatRow> rows;
  rows.reserve(static_cast<std::size_t>(result->rows()));
  for (int i = 0; i < result->rows(); ++i) {
    ChatRow row;
    row.message_id = ToInt64(result->Get(i, 0));
    row.room_id = room_id;
    row.player_id = result->Get(i, 1).value_or("");
    row.text = result->Get(i, 2).value_or("");
    row.sent_at_unix_millis = ToInt64(result->Get(i, 3));
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace

PgChatStore::PgChatStore(std::shared_ptr<pg::Client> db) : db_(std::move(db)) {}

absl::StatusOr<ChatRow> PgChatStore::Append(const std::string& room_id,
                                            const std::string& player_id, const std::string& text,
                                            const std::string& notify_payload) {
  if (absl::Status valid = ValidateChatText(text); !valid.ok()) return valid;

  bool is_member = false;
  ChatRow row;
  row.room_id = room_id;
  row.player_id = player_id;
  row.text = text;

  const absl::Status committed = db_->InTransaction([&](pg::Transaction& txn) -> absl::Status {
    absl::StatusOr<pg::Result> locked = txn.Exec(kLockRoomForMember, {room_id, player_id});
    if (!locked.ok()) return locked.status();
    // Not an error: nothing was written, so committing an empty
    // transaction is cheaper than rolling one back.
    if (locked->rows() == 0) return absl::OkStatus();
    is_member = true;

    absl::StatusOr<pg::Result> inserted = txn.Exec(kInsert, {room_id, player_id, text});
    if (!inserted.ok()) return inserted.status();
    row.message_id = ToInt64(inserted->Get(0, 0));
    row.sent_at_unix_millis = ToInt64(inserted->Get(0, 1));

    return txn
        .Exec(kPruneAndNotify,
              {room_id, std::to_string(kChatHistoryLimit), ChatChannel(room_id), notify_payload})
        .status();
  });

  if (!committed.ok()) return committed;
  if (!is_member) return NotAMemberError();
  return row;
}

absl::StatusOr<std::vector<ChatRow>> PgChatStore::LoadRecent(const std::string& room_id,
                                                             std::size_t limit) {
  if (limit == 0) return std::vector<ChatRow>();
  return ReadRows(db_->Exec(kLoadRecent, {room_id, std::to_string(limit)}), room_id);
}

absl::StatusOr<std::vector<ChatRow>> PgChatStore::LoadAfter(const std::string& room_id,
                                                            int64_t after_message_id,
                                                            std::size_t limit) {
  if (limit == 0) return std::vector<ChatRow>();
  return ReadRows(
      db_->Exec(kLoadAfter, {room_id, std::to_string(after_message_id), std::to_string(limit)}),
      room_id);
}

void PgChatStore::DropRoom(const std::string& /*room_id*/) {}

}  // namespace golf_hub
