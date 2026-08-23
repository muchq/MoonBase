#include "domains/games/apis/one_d4_worker/retention_policy.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "nlohmann/json.hpp"

namespace one_d4_worker {
namespace {

/// Every key the file must carry. Absent or non-integer is a failure, not a
/// default: a window silently falling back would delete on a schedule nobody
/// wrote down.
absl::StatusOr<absl::Duration> Seconds(const nlohmann::json& policy, std::string_view key) {
  const auto it = policy.find(key);
  if (it == policy.end() || !it->is_number_integer()) {
    return absl::InvalidArgumentError(absl::StrCat("retention policy is missing an integer ", key));
  }
  const int64_t seconds = it->get<int64_t>();
  if (seconds <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(key, " must be positive, got ", seconds));
  }
  return absl::Seconds(seconds);
}

}  // namespace

std::string RetentionPolicyPath(std::string_view argv0) {
  if (const char* override_path = std::getenv("ONE_D4_RETENTION_POLICY");
      override_path != nullptr && *override_path != '\0') {
    return override_path;
  }
  return absl::StrCat(argv0, ".runfiles/_main/domains/games/apis/one_d4/retention_policy.json");
}

absl::StatusOr<RetentionPolicy> LoadRetentionPolicy(const std::string& path) {
  std::ifstream file(path);
  if (!file.good()) {
    return absl::NotFoundError(absl::StrCat("cannot read retention policy at ", path));
  }
  std::ostringstream contents;
  contents << file.rdbuf();

  nlohmann::json parsed =
      nlohmann::json::parse(contents.str(), nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat("retention policy at ", path, " is not JSON"));
  }

  RetentionPolicy policy;
  struct Field {
    absl::Duration* field;
    std::string_view key;
  };
  const Field fields[] = {
      {&policy.period, "period_seconds"},
      {&policy.request, "request_seconds"},
      {&policy.stale_request, "stale_request_seconds"},
      {&policy.lease, "lease_seconds"},
      {&policy.lease_renewal, "lease_renewal_seconds"},
      {&policy.max_run, "max_run_seconds"},
      {&policy.statement_timeout, "sweep_statement_timeout_seconds"},
  };
  for (const auto& [field, key] : fields) {
    const absl::StatusOr<absl::Duration> value = Seconds(parsed, key);
    if (!value.ok()) return value.status();
    *field = *value;
  }

  // The relationships, checked here rather than left to the file's author.
  // Each is a correctness constraint the README argues for, and a policy that
  // violates one is worse than no policy: it deletes on a schedule that
  // contradicts itself.
  if (policy.request <= policy.period) {
    return absl::InvalidArgumentError(
        "request_seconds must exceed period_seconds: game_features.request_id is a foreign key, "
        "so a request has to outlive the games pointing at it");
  }
  if (policy.lease >= policy.stale_request) {
    return absl::InvalidArgumentError(
        "lease_seconds must be below stale_request_seconds: one asks whether the owner is still "
        "there, the other whether anyone is serving this at all");
  }
  if (policy.max_run <= policy.stale_request) {
    return absl::InvalidArgumentError(
        "max_run_seconds must exceed stale_request_seconds: a ceiling below the staleness window "
        "cuts runs short of the window used to decide whether anything is happening");
  }
  if (policy.lease_renewal * 4 > policy.lease) {
    return absl::InvalidArgumentError(
        "lease_renewal_seconds must leave four renewals inside a lease, or every lease lapses "
        "between beats and healthy workers lose their ranges");
  }
  return policy;
}

}  // namespace one_d4_worker
