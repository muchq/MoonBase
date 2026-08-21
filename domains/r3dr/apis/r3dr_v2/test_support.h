#ifndef DOMAINS_R3DR_APIS_R3DR_V2_TEST_SUPPORT_H
#define DOMAINS_R3DR_APIS_R3DR_V2_TEST_SUPPORT_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"
#include "domains/r3dr/apis/r3dr_v2/shortener.h"
#include "domains/r3dr/apis/r3dr_v2/url_store.h"

namespace r3dr_v2 {

inline constexpr absl::Time kNow = absl::FromUnixMillis(1755000000000);

/// The one in-memory UrlStore all suites share.
class FakeUrlStore final : public UrlStore {
 public:
  absl::StatusOr<std::string> Insert(const std::string& long_url, absl::Time expires_at) override {
    ++inserts;
    last_expires_at = expires_at;
    targets[next_slug] = Target{long_url, expires_at};
    return next_slug;
  }

  absl::StatusOr<std::optional<Target>> Lookup(const std::string& slug) override {
    ++lookups;
    if (fail_lookups) {
      return absl::UnavailableError("password authentication failed for host shared_postgres");
    }
    const auto found = targets.find(slug);
    if (found == targets.end() || found->second.expires_at <= now) {
      return std::optional<Target>();
    }
    return std::optional<Target>(found->second);
  }

  std::string next_slug = "AQA";
  std::map<std::string, Target> targets;
  absl::Time now = kNow;
  absl::Time last_expires_at;
  bool fail_lookups = false;
  int inserts = 0;
  int lookups = 0;
};

inline std::shared_ptr<Shortener> MakeShortener(std::shared_ptr<UrlStore> store,
                                                std::function<absl::Time()> now) {
  return std::make_shared<Shortener>(
      std::move(store),
      std::make_shared<Shortener::Cache>(
          "url_cache", 100,
          std::make_shared<futility::otel::CapturingMetricsRecorder>("r3dr_v2_test")),
      std::move(now));
}

}  // namespace r3dr_v2

#endif  // DOMAINS_R3DR_APIS_R3DR_V2_TEST_SUPPORT_H
