#ifndef DOMAINS_IILI_APIS_IILI_PG_URL_STORE_H
#define DOMAINS_IILI_APIS_IILI_PG_URL_STORE_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/iili/apis/iili/url_store.h"
#include "domains/platform/libs/pg/pg.h"

namespace iili {

/// The urls table. Two clients because pg::Client is one serialized
/// connection — a shared one would queue every redirect behind every
/// shorten. Tests pass the same client twice.
class PgUrlStore final : public UrlStore {
 public:
  PgUrlStore(std::shared_ptr<pg::Client> reads, std::shared_ptr<pg::Client> writes)
      : reads_(std::move(reads)), writes_(std::move(writes)) {}

  /// nextval, encode, insert. ON CONFLICT (id) DO UPDATE with
  /// RETURNING (xmax = 0) tells fresh from conflict in one statement:
  /// a conflict row identical to ours is pg::Client's reconnect-retry
  /// replaying (success); any other row means the sequence is behind
  /// the table — refuse rather than alias a slug. A retried nextval
  /// just burns a sequence gap.
  absl::StatusOr<std::string> Insert(const std::string& long_url, absl::Time expires_at) override;

  absl::StatusOr<std::optional<Target>> Lookup(const std::string& slug) override;

 private:
  std::shared_ptr<pg::Client> reads_;
  std::shared_ptr<pg::Client> writes_;
};

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_PG_URL_STORE_H
