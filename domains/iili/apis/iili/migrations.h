#ifndef DOMAINS_IILI_APIS_IILI_MIGRATIONS_H
#define DOMAINS_IILI_APIS_IILI_MIGRATIONS_H

#include "absl/status/status.h"
#include "domains/platform/libs/pg/pg.h"

namespace iili {

/// Idempotent statements run at startup (fail-fast) and in every DB test's
/// SetUp. Schema changes append in migrations.cc; every statement must stay
/// safe to re-run.
absl::Status RunMigrations(pg::Client& db);

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_MIGRATIONS_H
