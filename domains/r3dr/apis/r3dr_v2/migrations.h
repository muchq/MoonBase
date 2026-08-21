#ifndef DOMAINS_R3DR_APIS_R3DR_V2_MIGRATIONS_H
#define DOMAINS_R3DR_APIS_R3DR_V2_MIGRATIONS_H

#include "absl/status/status.h"
#include "domains/platform/libs/pg/pg.h"

namespace r3dr_v2 {

/// Idempotent statements run at startup (fail-fast) and in every DB test's
/// SetUp. Schema changes append in migrations.cc; every statement must stay
/// safe to re-run.
absl::Status RunMigrations(pg::Client& db);

}  // namespace r3dr_v2

#endif  // DOMAINS_R3DR_APIS_R3DR_V2_MIGRATIONS_H
