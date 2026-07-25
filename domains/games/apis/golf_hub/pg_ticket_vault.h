#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_PG_TICKET_VAULT_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_PG_TICKET_VAULT_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

/// TicketVault on the hub's postgres database (#1194 step 1): credentials
/// survive restarts and are visible to every instance, so the session
/// mint and the WebSocket dial no longer have to land on the same
/// process. Spend stays a single-row DELETE ... RETURNING — the
/// single-use semantic, now cross-instance.
///
/// Rows hold sha256 hashes of the tokens (hashed in SQL — postgres has
/// sha256() built in), so an at-rest dump leaks no live credential.
/// Expired rows are purged lazily on mints, like the in-memory vault.
/// Lookups fail closed on database errors (logged); Issue* surface them.
class PgTicketVault final : public TicketVault {
 public:
  PgTicketVault(std::shared_ptr<pg::Client> db, std::chrono::seconds ticket_ttl,
                std::chrono::seconds resume_ttl);

  absl::StatusOr<std::string> IssueTicket(const std::string& player_id) override;
  absl::StatusOr<std::string> IssueResumeToken(const std::string& player_id) override;
  bool PeekTicket(const std::string& ticket) const override;
  std::optional<std::string> SpendTicket(const std::string& ticket) override;
  std::optional<std::string> ResolveResumeToken(const std::string& token) const override;

 private:
  const std::shared_ptr<pg::Client> db_;
  const std::chrono::seconds ticket_ttl_;
  const std::chrono::seconds resume_ttl_;
};

}  // namespace golf_hub

#endif
