#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_OPTIONS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_OPTIONS_H

#include <string>

#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/retention_policy.h"

namespace one_d4_worker {

/// The poller's share of the shared retention policy, as worker_main applies
/// it at startup.
///
/// A function rather than three assignments in main, because main has no test
/// target: every wiring choice in it is one nothing would notice changing
/// (#1406), and these three decide whether a healthy worker keeps the range it
/// is working on. Swapping lease and lease_renewal renews every five minutes
/// on a seventy-five second lease — every lease lapses between beats, and the
/// fleet loses ranges it is still indexing.
///
/// Its own target rather than part of :poller, which would put the policy
/// loader — and the JSON parser behind it — in the compile of all 29 targets
/// that poll a queue but never read a file.
Poller::Options PollerOptionsFrom(const RetentionPolicy& policy, std::string owner);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_POLLER_OPTIONS_H
