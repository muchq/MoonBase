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
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace golf_hub
