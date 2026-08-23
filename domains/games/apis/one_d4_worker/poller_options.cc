#include "domains/games/apis/one_d4_worker/poller_options.h"

#include <string>
#include <utility>

namespace one_d4_worker {

Poller::Options PollerOptionsFrom(const RetentionPolicy& policy, std::string owner) {
  Poller::Options options;
  options.owner = std::move(owner);
  options.lease = policy.lease;
  options.renew_every = policy.lease_renewal;
  options.max_run = policy.max_run;
  return options;
}

}  // namespace one_d4_worker
