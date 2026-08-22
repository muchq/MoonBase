#include "domains/iili/apis/iili/migrations.h"

namespace iili {

absl::Status RunMigrations(pg::Client& db) {
  // url_ids is standalone (not an identity column) because the slug is
  // derived from the id in C++ before the row exists. The CHECK is a loose
  // byte-level net behind the model's @length(11, 1000) in code points.
  // Creates first — the data fixes below reference both relations.
  static constexpr const char* kStatements[] = {
      R"sql(CREATE SEQUENCE IF NOT EXISTS url_ids AS BIGINT START WITH 1)sql",
      R"sql(CREATE TABLE IF NOT EXISTS urls (
          id         bigint PRIMARY KEY,
          short_url  text NOT NULL UNIQUE,
          long_url   text NOT NULL CHECK (octet_length(long_url) BETWEEN 11 AND 4000),
          created_at timestamptz NOT NULL DEFAULT now(),
          expires_at timestamptz NOT NULL
      ))sql",
      // For the expired-row sweep (#373); reads ride the short_url index.
      R"sql(CREATE INDEX IF NOT EXISTS idx_urls_expires_at ON urls (expires_at))sql",
      // The Go shortener minted from its own url_ids with this same encoder;
      // the floor holds v2's ids clear of the slugs it issued. GREATEST keeps
      // re-runs monotone.
      R"sql(SELECT setval('url_ids',
          GREATEST((SELECT last_value FROM url_ids), 1000000), true))sql",
      // The floor only governs future ids; rows minted below it (pre-floor
      // v2 deploys) sit in the shared encoder's low space. Clear them so
      // Lookup's 404 contract holds. No-op once every row is above the floor.
      R"sql(DELETE FROM urls WHERE id < 1000000)sql",
  };
  for (const char* statement : kStatements) {
    if (auto result = db.Exec(statement); !result.ok()) return result.status();
  }
  return absl::OkStatus();
}

}  // namespace iili
