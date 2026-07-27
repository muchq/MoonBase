#include "domains/games/apis/golf_hub/migrations.h"

namespace golf_hub {

absl::Status RunMigrations(pg::Client& db) {
  // Credentials (#1194 step 1). Rows hold sha256 hashes of the tokens
  // (hashed in SQL by PgTicketVault), never the tokens themselves. The
  // expires_at indexes keep the purge-on-mint deletes cheap.
  static constexpr const char* kStatements[] = {
      R"sql(CREATE TABLE IF NOT EXISTS tickets (
          ticket_hash text PRIMARY KEY,
          player_id   text NOT NULL,
          expires_at  timestamptz NOT NULL
      ))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_tickets_expires_at
          ON tickets (expires_at))sql",
      R"sql(CREATE TABLE IF NOT EXISTS resume_tokens (
          token_hash  text PRIMARY KEY,
          player_id   text NOT NULL,
          expires_at  timestamptz NOT NULL
      ))sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_resume_tokens_expires_at
          ON resume_tokens (expires_at))sql",
      // Rooms, membership, and games (#1194 step 2). game state is the
      // serialized engine value (game_state_serde schema); version is the
      // optimistic-concurrency counter the hub owns. Game codes are only
      // unique within a room, hence the composite keys. Deleting a room
      // cascades — the hub stages one DeleteRoom when the last member
      // leaves.
      R"sql(CREATE TABLE IF NOT EXISTS rooms (
          room_id text PRIMARY KEY
      ))sql",
      R"sql(CREATE TABLE IF NOT EXISTS room_members (
          room_id      text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          player_id    text NOT NULL,
          connected    boolean NOT NULL,
          games_played integer NOT NULL,
          games_won    integer NOT NULL,
          total_score  integer NOT NULL,
          PRIMARY KEY (room_id, player_id)
      ))sql",
      R"sql(CREATE TABLE IF NOT EXISTS games (
          room_id text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          game_id text NOT NULL,
          roster  jsonb NOT NULL,
          state   jsonb,
          version bigint NOT NULL,
          PRIMARY KEY (room_id, game_id)
      ))sql",
      // Room chat (#1226). message_id is the ordering key and the
      // identity is global, not per-room, so one sequence orders every
      // room's history; sent_at is for display. Bodies are bounded here
      // as well as in ValidateChatText because retention counts rows,
      // not bytes. Chat dies with its room and survives its author
      // leaving, so the cascade hangs off rooms and there is no
      // reference to room_members.
      R"sql(CREATE TABLE IF NOT EXISTS room_chat_messages (
          message_id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
          room_id    text NOT NULL REFERENCES rooms (room_id) ON DELETE CASCADE,
          player_id  text NOT NULL,
          body       text NOT NULL CHECK (octet_length(body) BETWEEN 1 AND 500),
          sent_at    timestamptz NOT NULL DEFAULT clock_timestamp()
      ))sql",
      R"sql(ALTER TABLE room_chat_messages
          ALTER COLUMN sent_at SET DEFAULT clock_timestamp())sql",
      R"sql(CREATE INDEX IF NOT EXISTS idx_room_chat_messages_room
          ON room_chat_messages (room_id, message_id))sql",
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace golf_hub
