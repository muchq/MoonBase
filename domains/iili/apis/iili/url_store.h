#ifndef DOMAINS_IILI_APIS_IILI_URL_STORE_H
#define DOMAINS_IILI_APIS_IILI_URL_STORE_H

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace iili {

/// A live redirect target. expires_at rides along so a cached slug can go
/// dark on time.
struct Target {
  std::string long_url;
  absl::Time expires_at;
};

/// The slug table; PgUrlStore in production, fakes in tests.
class UrlStore {
 public:
  virtual ~UrlStore() = default;

  /// Mints a fresh slug for long_url; the store owns id assignment.
  virtual absl::StatusOr<std::string> Insert(const std::string& long_url,
                                             absl::Time expires_at) = 0;

  /// nullopt for unknown or expired — the expiry predicate is the store's,
  /// so no caller can forget it.
  virtual absl::StatusOr<std::optional<Target>> Lookup(const std::string& slug) = 0;
};

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_URL_STORE_H
