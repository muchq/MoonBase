#ifndef DOMAINS_R3DR_APIS_R3DR_V2_PG_URL_STORE_H
#define DOMAINS_R3DR_APIS_R3DR_V2_PG_URL_STORE_H

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/platform/libs/pg/pg.h"
#include "domains/r3dr/apis/r3dr_v2/url_store.h"

namespace r3dr_v2 {

/// The urls table. Two clients because pg::Client is one serialized
/// connection — a shared one would queue every redirect behind every
/// shorten. Tests pass the same client twice.
class PgUrlStore final : public UrlStore {
 public:
  PgUrlStore(std::shared_ptr<pg::Client> reads, std::shared_ptr<pg::Client> writes)
      : reads_(std::move(reads)), writes_(std::move(writes)) {}

  /// nextval, encode, insert. ON CONFLICT (id) DO NOTHING makes
  /// pg::Client's reconnect-retry safe: a replayed insert reads as zero
  /// rows, which is success — only this process ever holds that id. A
  /// retried nextval just burns a sequence gap.
  absl::StatusOr<std::string> Insert(const std::string& long_url, absl::Time expires_at) override;

  absl::StatusOr<std::optional<Target>> Lookup(const std::string& slug) override;

 private:
  std::shared_ptr<pg::Client> reads_;
  std::shared_ptr<pg::Client> writes_;
};

}  // namespace r3dr_v2

#endif  // DOMAINS_R3DR_APIS_R3DR_V2_PG_URL_STORE_H
