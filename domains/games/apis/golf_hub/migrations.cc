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
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace golf_hub
