#include "domains/r3dr/apis/r3dr_v2/migrations.h"

namespace r3dr_v2 {

absl::Status RunMigrations(pg::Client& db) {
  // url_ids is standalone (not an identity column) because the slug is
  // derived from the id in C++ before the row exists. The CHECK is a loose
  // byte-level net behind the model's @length(11, 1000) in code points.
  static constexpr const char* kStatements[] = {
      R"sql(CREATE SEQUENCE IF NOT EXISTS url_ids AS BIGINT START WITH 1)sql",
      // v1 minted from its own url_ids with the same encoder, so a v2 id v1
      // already used aliases an old r3dr.net link to the wrong URL once the
      // worker owns the domain. The floor keeps v2's id space above v1's
      // lifetime mint count: dead v1 links 404 instead of misdirecting.
      // GREATEST keeps the bump monotone across re-runs.
      R"sql(SELECT setval('url_ids',
          GREATEST((SELECT last_value FROM url_ids), 1000000), true))sql",
      R"sql(CREATE TABLE IF NOT EXISTS urls (
          id         bigint PRIMARY KEY,
          short_url  text NOT NULL UNIQUE,
          long_url   text NOT NULL CHECK (octet_length(long_url) BETWEEN 11 AND 4000),
          created_at timestamptz NOT NULL DEFAULT now(),
          expires_at timestamptz NOT NULL
      ))sql",
      // For the expired-row sweep (#373); reads ride the short_url index.
      R"sql(CREATE INDEX IF NOT EXISTS idx_urls_expires_at ON urls (expires_at))sql",
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace r3dr_v2
