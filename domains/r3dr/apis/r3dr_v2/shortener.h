#ifndef DOMAINS_R3DR_APIS_R3DR_V2_SHORTENER_H
#define DOMAINS_R3DR_APIS_R3DR_V2_SHORTENER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/platform/libs/aura/cache.h"
#include "domains/r3dr/apis/r3dr_v2/url_store.h"

namespace r3dr_v2 {

inline constexpr absl::Duration kMaxLifetime = absl::Hours(24 * 30);

/// Past or more than 30 days out -> kInvalidArgument naming the rule.
absl::StatusOr<absl::Time> ResolveExpiry(int64_t expires_at_millis, absl::Time now);

/// Expiry policy + slug cache over the store. Cache entries are positive
/// only and carry their expiry: a slug is never retargeted, so {url,
/// expires_at} is a pure function of the slug (aura::Cache's keep-first
/// insert contract), and an entry past its expiry answers "gone" with no
/// store trip. Misses are not cached — the LRU has no invalidation, and a
/// scanner probing the predictable next ids could pre-poison slugs about to
/// be minted. Per-client rate limiting bounds single-client scans; a
/// distributed miss flood is accepted risk at this scale (watch
/// cache_misses).
class Shortener {
 public:
  using Cache = aura::Cache<std::string, Target>;

  Shortener(std::shared_ptr<UrlStore> store, std::shared_ptr<Cache> cache,
            std::function<absl::Time()> now)
      : store_(std::move(store)), cache_(std::move(cache)), now_(std::move(now)) {}

  absl::StatusOr<std::string> Shorten(const std::string& long_url, int64_t expires_at_millis);

  /// nullopt for unknown, expired, or sub-3-char slugs (real slugs are
  /// 3/6/11 chars; the short ones skip the store).
  absl::StatusOr<std::optional<std::string>> Resolve(const std::string& slug);

 private:
  std::shared_ptr<UrlStore> store_;
  std::shared_ptr<Cache> cache_;
  std::function<absl::Time()> now_;
};

}  // namespace r3dr_v2

#endif  // DOMAINS_R3DR_APIS_R3DR_V2_SHORTENER_H
