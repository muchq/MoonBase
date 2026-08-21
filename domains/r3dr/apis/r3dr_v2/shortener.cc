#include "domains/r3dr/apis/r3dr_v2/shortener.h"

#include <optional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace r3dr_v2 {

absl::StatusOr<absl::Time> ResolveExpiry(int64_t expires_at_millis, absl::Time now) {
  const absl::Time requested = absl::FromUnixMillis(expires_at_millis);
  if (requested <= now) {
    return absl::InvalidArgumentError("expiresAt is in the past");
  }
  if (requested > now + kMaxLifetime) {
    return absl::InvalidArgumentError(absl::StrCat("expiresAt is more than ",
                                                   absl::ToInt64Hours(kMaxLifetime) / 24,
                                                   " days out — the maximum lifetime"));
  }
  return requested;
}

absl::StatusOr<std::string> Shortener::Shorten(const std::string& long_url,
                                               int64_t expires_at_millis) {
  absl::StatusOr<absl::Time> expires_at = ResolveExpiry(expires_at_millis, now_());
  if (!expires_at.ok()) {
    return expires_at.status();
  }
  absl::StatusOr<std::string> slug = store_->Insert(long_url, *expires_at);
  if (!slug.ok()) {
    // The wire answer is a fixed 500; this is the operator's only record.
    LOG(WARNING) << "shorten failed: " << slug.status();
    return slug.status();
  }
  cache_->insert(*slug, Target{long_url, *expires_at});
  return slug;
}

absl::StatusOr<std::optional<std::string>> Shortener::Resolve(const std::string& slug) {
  if (slug.size() < 3) {
    return std::optional<std::string>();
  }
  if (const std::optional<Target> cached = cache_->get(slug); cached.has_value()) {
    if (now_() >= cached->expires_at) {
      return std::optional<std::string>();
    }
    return std::optional<std::string>(cached->long_url);
  }
  absl::StatusOr<std::optional<Target>> looked = store_->Lookup(slug);
  if (!looked.ok()) {
    LOG(WARNING) << "resolve failed: " << looked.status();
    return looked.status();
  }
  if (!looked->has_value()) {
    return std::optional<std::string>();
  }
  cache_->insert(slug, **looked);
  return std::optional<std::string>((*looked)->long_url);
}

}  // namespace r3dr_v2
