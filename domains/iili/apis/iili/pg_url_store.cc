#include "domains/iili/apis/iili/pg_url_store.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "domains/iili/apis/iili/encoding.h"

namespace iili {
namespace {

// $1 = id, $2 = slug, $3 = long url, $4 = expiry epoch millis. One row
// always: xmax = 0 marks a fresh insert; on conflict the returned row
// tells a replay (identical to ours) from an import that outran the
// sequence (someone else's), atomically.
constexpr char kInsert[] = R"sql(
    INSERT INTO urls (id, short_url, long_url, expires_at)
    VALUES ($1::bigint, $2, $3, to_timestamp($4::bigint / 1000.0))
    ON CONFLICT (id) DO UPDATE SET long_url = urls.long_url
    RETURNING (xmax = 0) AS inserted, short_url, long_url,
              (extract(epoch FROM expires_at) * 1000)::bigint)sql";

// Millis out, so the value reaches absl::Time without a timestamp parse.
constexpr char kLookup[] = R"sql(
    SELECT long_url, (extract(epoch FROM expires_at) * 1000)::bigint
    FROM urls
    WHERE short_url = $1 AND expires_at > now())sql";

}  // namespace

absl::StatusOr<std::string> PgUrlStore::Insert(const std::string& long_url, absl::Time expires_at) {
  auto next = writes_->Exec("SELECT nextval('url_ids')");
  if (!next.ok()) {
    return next.status();
  }
  int64_t id = 0;
  const std::optional<std::string> raw = next->Get(0, 0);
  if (!raw.has_value() || !absl::SimpleAtoi(*raw, &id)) {
    return absl::InternalError("url_ids returned no id");
  }
  absl::StatusOr<std::string> slug = EncodeId(id);
  if (!slug.ok()) {
    // A negative sequence value is server-side corruption, not client fault.
    return absl::InternalError(std::string(slug.status().message()));
  }
  const std::string expires_millis = absl::StrCat(absl::ToUnixMillis(expires_at));
  auto inserted = writes_->Exec(kInsert, {absl::StrCat(id), *slug, long_url, expires_millis});
  if (!inserted.ok()) {
    return inserted.status();
  }
  if (inserted->rows() != 1) {
    return absl::InternalError("insert returned no row");
  }
  // A conflict row identical to ours is pg::Client's reconnect-retry
  // replaying this statement; anything else is an import's row.
  const bool fresh = inserted->Get(0, 0) == std::optional<std::string>("t");
  const bool replay = inserted->Get(0, 1) == std::optional<std::string>(*slug) &&
                      inserted->Get(0, 2) == std::optional<std::string>(long_url) &&
                      inserted->Get(0, 3) == std::optional<std::string>(expires_millis);
  if (!fresh && !replay) {
    return absl::InternalError("url_ids is behind the urls table; refusing to alias a slug");
  }
  return slug;
}

absl::StatusOr<std::optional<Target>> PgUrlStore::Lookup(const std::string& slug) {
  auto result = reads_->Exec(kLookup, {slug});
  if (!result.ok()) {
    return result.status();
  }
  if (result->rows() == 0) {
    return std::optional<Target>();
  }
  std::optional<std::string> long_url = result->Get(0, 0);
  const std::optional<std::string> expires_millis = result->Get(0, 1);
  int64_t millis = 0;
  if (!long_url.has_value() || !expires_millis.has_value() ||
      !absl::SimpleAtoi(*expires_millis, &millis)) {
    return absl::InternalError("urls row came back unreadable");
  }
  return std::optional<Target>(Target{*std::move(long_url), absl::FromUnixMillis(millis)});
}

}  // namespace iili
