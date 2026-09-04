#include "domains/games/apis/games_hub/pg_ticket_vault.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace games_hub {
namespace {

// Mint's statements, per table. The purges are the lazy expiry sweep the
// in-memory vault does on every mint. In the inserts, $1 = token,
// $2 = player id, $3 = ttl seconds, and RETURNING distinguishes an
// insert from an id collision (no row), which Mint below retries with a
// fresh token.
constexpr char kPurgeTickets[] = "DELETE FROM tickets WHERE expires_at <= now()";
constexpr char kInsertTicket[] = R"sql(
    INSERT INTO tickets (ticket_hash, player_id, expires_at)
    VALUES (encode(sha256(convert_to($1, 'UTF8')), 'hex'), $2,
            now() + make_interval(secs => $3::double precision))
    ON CONFLICT (ticket_hash) DO NOTHING
    RETURNING 1)sql";
constexpr char kPurgeResumeTokens[] = "DELETE FROM resume_tokens WHERE expires_at <= now()";
constexpr char kInsertResumeToken[] = R"sql(
    INSERT INTO resume_tokens (token_hash, player_id, expires_at)
    VALUES (encode(sha256(convert_to($1, 'UTF8')), 'hex'), $2,
            now() + make_interval(secs => $3::double precision))
    ON CONFLICT (token_hash) DO NOTHING
    RETURNING 1)sql";

constexpr char kPeekTicket[] = R"sql(
    SELECT 1 FROM tickets
    WHERE ticket_hash = encode(sha256(convert_to($1, 'UTF8')), 'hex')
      AND expires_at > now())sql";
// Spend checks and consumes in one statement. If pg::Client retries it
// after a reconnect, a spend whose first execution landed re-reports as
// "no row": the ticket burns fail-closed rather than double-admitting.
constexpr char kSpendTicket[] = R"sql(
    DELETE FROM tickets
    WHERE ticket_hash = encode(sha256(convert_to($1, 'UTF8')), 'hex')
      AND expires_at > now()
    RETURNING player_id)sql";
constexpr char kResolveResumeToken[] = R"sql(
    SELECT player_id FROM resume_tokens
    WHERE token_hash = encode(sha256(convert_to($1, 'UTF8')), 'hex')
      AND expires_at > now())sql";

}  // namespace

PgTicketVault::PgTicketVault(std::shared_ptr<pg::Client> db, std::chrono::seconds ticket_ttl,
                             std::chrono::seconds resume_ttl)
    : db_(std::move(db)), ticket_ttl_(ticket_ttl), resume_ttl_(resume_ttl) {}

namespace {

absl::StatusOr<std::string> Mint(pg::Client& db, const char* purge_sql, const char* insert_sql,
                                 std::string_view prefix, const std::string& player_id,
                                 std::chrono::seconds ttl) {
  if (auto purged = db.Exec(purge_sql); !purged.ok()) return purged.status();
  // A 12-hex-digit id collides only against another *live* row, so even
  // three attempts is generosity; bounded in case something is deeply
  // wrong. The loop also absorbs pg::Client's reconnect retry: an insert
  // whose first execution landed hits ON CONFLICT on the retry and reads
  // as a collision — Mint just tries a fresh token and the orphan row
  // expires into the next purge.
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::string token = RandomId(prefix);
    auto inserted = db.Exec(insert_sql, {token, player_id, std::to_string(ttl.count())});
    if (!inserted.ok()) return inserted.status();
    if (inserted->rows() == 1) return token;
  }
  return absl::InternalError("credential id collided repeatedly");
}

}  // namespace

absl::StatusOr<std::string> PgTicketVault::IssueTicket(const std::string& player_id) {
  return Mint(*db_, kPurgeTickets, kInsertTicket, "t", player_id, ticket_ttl_);
}

absl::StatusOr<std::string> PgTicketVault::IssueResumeToken(const std::string& player_id) {
  return Mint(*db_, kPurgeResumeTokens, kInsertResumeToken, "rt", player_id, resume_ttl_);
}

bool PgTicketVault::PeekTicket(const std::string& ticket) const {
  auto result = db_->Exec(kPeekTicket, {ticket});
  if (!result.ok()) {
    LOG(WARNING) << "PeekTicket failing closed: " << result.status();
    return false;
  }
  return result->rows() > 0;
}

std::optional<std::string> PgTicketVault::SpendTicket(const std::string& ticket) {
  auto result = db_->Exec(kSpendTicket, {ticket});
  if (!result.ok()) {
    LOG(WARNING) << "SpendTicket failing closed: " << result.status();
    return std::nullopt;
  }
  if (result->rows() == 0) return std::nullopt;
  return result->Get(0, 0);
}

std::optional<std::string> PgTicketVault::ResolveResumeToken(const std::string& token) const {
  auto result = db_->Exec(kResolveResumeToken, {token});
  if (!result.ok()) {
    LOG(WARNING) << "ResolveResumeToken failing closed: " << result.status();
    return std::nullopt;
  }
  if (result->rows() == 0) return std::nullopt;
  return result->Get(0, 0);
}

}  // namespace games_hub
