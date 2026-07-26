#include "domains/games/apis/golf_hub/pg_chat_store.h"

#include <cstdlib>
#include <optional>
#include <utility>

#include "absl/status/status.h"

namespace golf_hub {
namespace {

// One statement, four effects. `room` takes the per-room lock and gates
// everything on the room still existing; `appended` inserts only for a
// current member; `pruned` trims to the retention window; the outer
// SELECT fires the notify once per inserted row, so a rejected append
// notifies nobody.
//
// The data-modifying CTEs all read the pre-statement snapshot, so
// `pruned` cannot see the row `appended` just wrote. That is why $4 is
// the window minus one: keep the newest N-1 rows that already existed
// and the new one makes N. It also has to reference `appended` in its
// WHERE — postgres runs every data-modifying CTE exactly once whether
// or not it is read, so an unguarded DELETE would trim on a rejected
// append too.
constexpr char kAppend[] = R"sql(
    WITH room AS (
      SELECT room_id FROM rooms WHERE room_id = $1 FOR UPDATE),
    appended AS (
      INSERT INTO room_chat_messages (room_id, player_id, body)
      SELECT room.room_id, $2, $3 FROM room
      WHERE EXISTS (
        SELECT 1 FROM room_members m WHERE m.room_id = $1 AND m.player_id = $2)
      RETURNING message_id, sent_at),
    pruned AS (
      DELETE FROM room_chat_messages c
      WHERE c.room_id = $1
        AND EXISTS (SELECT 1 FROM appended)
        AND c.message_id < (
          SELECT min(message_id) FROM (
            SELECT message_id FROM room_chat_messages
            WHERE room_id = $1 ORDER BY message_id DESC LIMIT $4::bigint) keep))
    SELECT message_id, (EXTRACT(EPOCH FROM sent_at) * 1000)::bigint, pg_notify($5, $6)
    FROM appended)sql";

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

  auto result =
      db_->ExecOnce(kAppend, {room_id, player_id, text, std::to_string(kChatHistoryLimit - 1),
                              ChatChannel(room_id), notify_payload});
  if (!result.ok()) return result.status();
  if (result->rows() == 0) {
    return absl::FailedPreconditionError("sender is not a member of the room");
  }

  ChatRow row;
  row.message_id = ToInt64(result->Get(0, 0));
  row.room_id = room_id;
  row.player_id = player_id;
  row.text = text;
  row.sent_at_unix_millis = ToInt64(result->Get(0, 1));
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
