#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_MIGRATIONS_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_MIGRATIONS_H

#include "absl/status/status.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

/// The hub's schema, applied at startup before the server takes traffic
/// (one_d4's Migration pattern: idempotent statements run in-service, no
/// external tool). Every statement must stay safe to re-run; schema
/// changes land as new statements appended here. Currently #1194 step 1:
/// the two credential tables.
absl::Status RunMigrations(pg::Client& db);

}  // namespace golf_hub

#endif
