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

namespace {

// The Go hub's word lists (players.WhimsicalIDGenerator), verbatim.
constexpr std::string_view kAdjectives[] = {"bouncy",  "giggly", "sparkly", "fuzzy",   "wiggly",
                                            "snuggly", "dreamy", "bubbly",  "twinkly", "jolly",
                                            "quirky",  "peppy",  "zesty",   "frisky",  "silly",
                                            "perky",   "cheeky", "zippy",   "groovy",  "jazzy"};
constexpr std::string_view kColors[] = {
    "lavender", "periwinkle", "coral",  "mint",       "peach",   "turquoise", "magenta",
    "cerulean", "lilac",      "salmon", "chartreuse", "crimson", "cobalt",    "amber",
    "jade",     "fuchsia",    "indigo", "teal",       "mauve",   "vermillion"};
constexpr std::string_view kAnimals[] = {
    "koala",  "kangaroo", "wombat",    "quokka",     "platypus", "echidna",  "wallaby",
    "bilby",  "numbat",   "possum",    "kookaburra", "cockatoo", "lorikeet", "galah",
    "budgie", "dingo",    "bandicoot", "pademelon",  "potoroo",  "glider"};

template <std::size_t N>
std::string_view Pick(absl::BitGen& gen, const std::string_view (&words)[N]) {
  return words[absl::Uniform<std::size_t>(gen, 0u, N)];
}

}  // namespace

std::string WhimsicalId() {
  thread_local absl::BitGen gen;
  constexpr char kSlug[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string id;
  id.append(Pick(gen, kAdjectives));
  id.push_back('-');
  id.append(Pick(gen, kColors));
  id.push_back('-');
  id.append(Pick(gen, kAnimals));
  id.push_back('-');
  for (int i = 0; i < 4; ++i) {
    id.push_back(kSlug[absl::Uniform<std::size_t>(gen, 0u, sizeof(kSlug) - 1)]);
  }
  return id;
}

std::string GameCode() {
  thread_local absl::BitGen gen;
  constexpr char kCode[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::string code;
  for (int i = 0; i < 6; ++i) {
    code.push_back(kCode[absl::Uniform<std::size_t>(gen, 0u, sizeof(kCode) - 1)]);
  }
  return code;
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
