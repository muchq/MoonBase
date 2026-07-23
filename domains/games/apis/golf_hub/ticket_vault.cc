#include "domains/games/apis/golf_hub/ticket_vault.h"

#include "absl/random/distributions.h"

namespace golf_hub {

std::string RandomId(std::string_view prefix) {
  thread_local absl::BitGen gen;
  constexpr char kHex[] = "0123456789abcdef";
  std::string id(prefix);
  id.push_back('-');
  for (int i = 0; i < 12; ++i) {
    id.push_back(kHex[absl::Uniform<uint32_t>(gen, 0u, 16u)]);
  }
  return id;
}

TicketVault::TicketVault(std::chrono::seconds ticket_ttl, std::chrono::seconds resume_ttl)
    : ticket_ttl_(ticket_ttl), resume_ttl_(resume_ttl) {}

std::string TicketVault::IssueTicket(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  return MintLocked(tickets_, player_id, ticket_ttl_, "t");
}

std::string TicketVault::IssueResumeToken(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  return MintLocked(resume_tokens_, player_id, resume_ttl_, "rt");
}

bool TicketVault::PeekTicket(const std::string& ticket) const {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = tickets_.find(ticket);
  return it != tickets_.end() && std::chrono::steady_clock::now() < it->second.deadline;
}

std::optional<std::string> TicketVault::SpendTicket(const std::string& ticket) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = tickets_.find(ticket);
  if (it == tickets_.end()) return std::nullopt;
  if (std::chrono::steady_clock::now() >= it->second.deadline) {
    tickets_.erase(it);
    return std::nullopt;
  }
  std::string player_id = std::move(it->second.player_id);
  tickets_.erase(it);
  return player_id;
}

std::optional<std::string> TicketVault::ResolveResumeToken(const std::string& token) const {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = resume_tokens_.find(token);
  if (it == resume_tokens_.end() || std::chrono::steady_clock::now() >= it->second.deadline) {
    return std::nullopt;
  }
  return it->second.player_id;
}

std::string TicketVault::MintLocked(Store& store, const std::string& player_id,
                                    std::chrono::seconds ttl, std::string_view prefix) {
  PurgeLocked(store);
  std::string token = RandomId(prefix);
  store[token] = Entry{player_id, std::chrono::steady_clock::now() + ttl};
  return token;
}

void TicketVault::PurgeLocked(Store& store) {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = store.begin(); it != store.end();) {
    it = now >= it->second.deadline ? store.erase(it) : std::next(it);
  }
}

}  // namespace golf_hub
