#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_POLICY_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_POLICY_H

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace one_d4_worker {

/// How long indexed data survives, and how long a claim on it is good for.
///
/// Read from one_d4's retention_policy.json at startup (#1424). The Java
/// service reads the same file off its classpath, so there is one set of
/// numbers and neither language holds a copy that can drift from it. What the
/// numbers mean is one_d4's README, under "Data Retention Policy".
///
/// No defaults: a zero-initialised policy would sweep everything older than
/// the epoch, so the only way to get one is to load it.
struct RetentionPolicy {
  absl::Duration period;
  absl::Duration request;
  absl::Duration stale_request;

  /// The lease vocabulary, which the poller uses rather than the sweep. One
  /// load feeds both: they are the same policy, and a worker whose renewal
  /// interval disagreed with the window the sweep reclaims against would lose
  /// ranges it was still working.
  absl::Duration lease;
  absl::Duration lease_renewal;
  absl::Duration max_run;

  /// Bounds each of the sweep's statements. The sweep holds no lease and
  /// nothing interrupts it, and every arm is idempotent, so a truncated pass
  /// costs an hour rather than correctness.
  absl::Duration statement_timeout;
};

/// Where the policy sits beside this binary. Derived from argv[0] because
/// pkg_tar ships data deps into the image's runfiles tree, so the path is
/// fixed relative to the binary in the container and under bazel test alike.
///
/// There is no override. A deployment that could point a worker at a
/// different file would be one whose windows no test has seen and whose Java
/// service — which has no equivalent — is quoting users different numbers.
std::string RetentionPolicyPath(std::string_view argv0);

/// Reads and validates the policy.
///
/// Fails rather than defaulting, and the caller is expected to exit: a worker
/// that cannot read its windows would otherwise pick some and start deleting
/// against them. The invariants are checked here for the same reason — a
/// request window shorter than the game window would delete rows the foreign
/// key still needs, and a lease at or above the staleness window makes the two
/// clocks the same question.
absl::StatusOr<RetentionPolicy> LoadRetentionPolicy(const std::string& path);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_RETENTION_POLICY_H
