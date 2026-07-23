#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_TICKET_VAULT_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_TICKET_VAULT_H

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/random/random.h"

namespace golf_hub {

/// A fresh "<prefix>-<12 hex>" identifier (room ids, tokens).
/// absl::BitGen randomness — unguessable enough for a game hub's opaque
/// ids; not a cryptographic claim.
std::string RandomId(std::string_view prefix);

/// A whimsical player id ("bouncy-coral-quokka-x9k2") — the Go hub's
/// user-visible naming, ported so beta players never see opaque ids.
/// Doubles as the display name.
std::string WhimsicalId();

/// A 6-char uppercase alphanumeric code — the Go hub's room/game id
/// format, kept for permalink compatibility.
std::string GameCode();

/// Single-process store for the hub's two credentials (smithy-cpp
/// ADR-0018's ticket pattern): tickets are single-use and short-lived —
/// minted by GetSession, checked by the gate pre-101 (PeekTicket), spent
/// exactly once by the Play handler (SpendTicket). Resume tokens are
/// multi-use and long-lived — a reconnect exchanges one for a fresh
/// ticket and gets the same playerId back. All state is in-memory: a
/// restart forgets every credential, which matches the Go hub it
/// replaces (its JWT secret rotated on restart).
///
/// Thread-safe; expired entries are purged lazily on mints.
class TicketVault {
 public:
  TicketVault(std::chrono::seconds ticket_ttl, std::chrono::seconds resume_ttl);

  std::string IssueTicket(const std::string& player_id);
  std::string IssueResumeToken(const std::string& player_id);

  /// Unexpired and unspent. Read-only — the gate's pre-101 check;
  /// SpendTicket remains the single-use authority.
  bool PeekTicket(const std::string& ticket) const;

  /// At most one caller ever receives the player id.
  std::optional<std::string> SpendTicket(const std::string& ticket);

  /// Multi-use until expiry.
  std::optional<std::string> ResolveResumeToken(const std::string& token) const;

 private:
  struct Entry {
    std::string player_id;
    std::chrono::steady_clock::time_point deadline;
  };
  using Store = std::unordered_map<std::string, Entry>;

  std::string MintLocked(Store& store, const std::string& player_id, std::chrono::seconds ttl,
                         std::string_view prefix);
  static void PurgeLocked(Store& store);

  const std::chrono::seconds ticket_ttl_;
  const std::chrono::seconds resume_ttl_;
  mutable std::mutex mu_;
  Store tickets_;
  Store resume_tokens_;
};

}  // namespace golf_hub

#endif
