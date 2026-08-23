#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_CLAIM_REF_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_CLAIM_REF_H

#include <string_view>

namespace one_d4_worker {

/// Which row a fenced write is for, and under whose claim.
///
/// Both fields are deliberately left without default member initializers:
/// under -Wextra a designated initializer that names only one of them is a
/// compile error, which is what stops `{.id = …}` from fencing against an
/// empty owner. Non-owning — the views must outlive the call.
struct ClaimRef {
  std::string_view id;
  std::string_view owner;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_CLAIM_REF_H
