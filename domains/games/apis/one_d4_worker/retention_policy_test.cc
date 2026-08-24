#include "domains/games/apis/one_d4_worker/retention_policy.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace one_d4_worker {
namespace {

using ::testing::Test;

constexpr char kShippedPolicy[] = "domains/games/apis/one_d4/retention_policy.json";

/// Writes a policy into the test's own temp dir and returns the path. Takes
/// the whole document so a case can be malformed, not just wrong.
std::string PolicyFile(const std::string& name, const std::string& contents) {
  const std::string path = absl::StrCat(::testing::TempDir(), "/", name, ".json");
  std::ofstream out(path, std::ios::trunc);
  out << contents;
  out.close();
  return path;
}

/// The shipped policy with one key overridden, so a case says only what it is
/// about. Every other window stays valid, which matters: the invariants are
/// checked as a set, and a hand-written minimal file would trip whichever one
/// it happened to violate first rather than the one under test.
///
/// Read from the shipped file rather than transcribed, so a key added there
/// is covered here without anyone remembering to copy it across.
std::string PolicyWith(const std::string& key, const std::string& value) {
  std::ifstream shipped(kShippedPolicy);
  EXPECT_TRUE(shipped.good()) << "cannot read " << kShippedPolicy;
  std::ostringstream buffer;
  buffer << shipped.rdbuf();
  std::string doc = buffer.str();
  const std::string needle = absl::StrCat("\"", key, "\": ");
  const size_t at = doc.find(needle);
  EXPECT_NE(at, std::string::npos) << key << " is not a key of the shipped policy";
  const size_t start = at + needle.size();
  const size_t end = doc.find_first_of(",}\n", start);
  return doc.replace(start, end - start, value);
}

/// The shipped policy with one key deleted outright, rather than present and
/// unusable. The absent-key branch is the one a hand-edit actually produces,
/// and it is a different branch from a key left in place holding nonsense.
std::string PolicyWithout(const std::string& key) {
  const std::string doc = PolicyWith(key, "0");
  const std::string needle = absl::StrCat("\"", key, "\": ");
  const size_t at = doc.find(needle);
  EXPECT_NE(at, std::string::npos) << key << " is not a key of the shipped policy";
  const size_t end = doc.find('\n', at);
  return doc.substr(0, at) + (end == std::string::npos ? "" : doc.substr(end));
}

/// The one that matters most: whatever is checked in has to be loadable, and
/// nothing else in the build would notice if it were not. A policy that fails
/// validation does not fail a deploy — it fails the worker's next start, after
/// the image is out.
TEST(RetentionPolicy, TheShippedPolicyLoads) {
  const auto policy = LoadRetentionPolicy(kShippedPolicy);
  ASSERT_TRUE(policy.ok()) << policy.status();

  // The numbers the README publishes in prose. Changing a window means
  // changing this, and changing this means finding the sentences that stated
  // the old one — API.md's "30 days" and its "23-day gap" paragraph, the
  // README retention table, the web app's panel note.
  EXPECT_EQ(policy->period, absl::Hours(24 * 7));
  EXPECT_EQ(policy->request, absl::Hours(24 * 30));
  EXPECT_EQ(policy->stale_request, absl::Hours(1));
  EXPECT_EQ(policy->lease, absl::Minutes(5));
  EXPECT_EQ(policy->lease_renewal, absl::Seconds(75));
  EXPECT_EQ(policy->max_run, absl::Hours(6));
  EXPECT_EQ(policy->max_attempts, 3);
  EXPECT_EQ(policy->statement_timeout, absl::Seconds(120));
}

TEST(RetentionPolicy, AMissingFileIsNotFound) {
  const auto policy = LoadRetentionPolicy("/nonexistent/retention_policy.json");
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kNotFound);
}

TEST(RetentionPolicy, GarbageIsRejectedRatherThanPartiallyRead) {
  const auto policy = LoadRetentionPolicy(PolicyFile("garbage", "{ not json at all"));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
}

/// A JSON array parses fine and has none of the keys. Rejected as "not JSON"
/// rather than as seven missing windows, because that is the actual mistake.
TEST(RetentionPolicy, ADocumentThatIsNotAnObjectIsRejected) {
  const auto policy = LoadRetentionPolicy(PolicyFile("array", "[604800, 2592000]"));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);

  // The message, not just the code: an array reaches the key lookup happily —
  // nlohmann's find on a non-object returns end() — so without the shape check
  // this fails as "missing period_seconds", which sends the reader looking for
  // a key that is not the problem.
  EXPECT_NE(policy.status().message().find("is not JSON"), std::string::npos)
      << policy.status().message();
}

TEST(RetentionPolicy, EveryWindowIsRequired) {
  for (const char* key : {"period_seconds", "request_seconds", "stale_request_seconds",
                          "lease_seconds", "lease_renewal_seconds", "max_run_seconds",
                          "max_attempts", "sweep_statement_timeout_seconds"}) {
    // Both shapes, because they take different branches of the same check and
    // a key deleted outright is the likelier edit. Nulling one leaves it
    // present and non-integral; dropping it leaves the lookup finding nothing,
    // and a loader that rejected only the first would fall back silently on
    // the second — deleting on a schedule nobody wrote down.
    for (const std::string& doc : {PolicyWith(key, "null"), PolicyWithout(key)}) {
      const auto policy = LoadRetentionPolicy(PolicyFile(absl::StrCat("missing_", key), doc));
      ASSERT_FALSE(policy.ok()) << key << " was accepted; the document was " << doc;
      EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
      EXPECT_NE(policy.status().message().find(key), std::string::npos)
          << "the failure should name the key: " << policy.status().message();
    }
  }
}

/// Not just "an integer". A window given as seconds-with-a-fraction is a unit
/// mistake, and one given as a string is a quoting mistake; both would read as
/// some number if they were coerced.
TEST(RetentionPolicy, AWindowThatIsNotAnIntegerIsRejected) {
  for (const char* value : {"\"300\"", "300.5", "true"}) {
    const auto policy =
        LoadRetentionPolicy(PolicyFile("nonint", PolicyWith("lease_seconds", value)));
    ASSERT_FALSE(policy.ok()) << value << " was accepted as a window";
    EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
  }
}

/// Zero and negative are the shapes a hand-edit produces — "disable this by
/// setting it to 0" — and a zero window sweeps everything older than the
/// instant of the sweep, which is everything.
TEST(RetentionPolicy, ANonPositiveWindowIsRejected) {
  for (const char* value : {"0", "-1"}) {
    const auto policy =
        LoadRetentionPolicy(PolicyFile("nonpositive", PolicyWith("period_seconds", value)));
    ASSERT_FALSE(policy.ok()) << value << " was accepted as a window";
    EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
  }
}

/// The attempt budget is a count rather than a window, and reaches the struct
/// through a different reader, so the same two rejections are checked on it
/// directly. A budget of zero makes every request unclaimable on arrival.
TEST(RetentionPolicy, ABadAttemptBudgetIsRejected) {
  for (const char* value : {"\"3\"", "3.5", "true", "0", "-1"}) {
    const auto policy =
        LoadRetentionPolicy(PolicyFile("attempts", PolicyWith("max_attempts", value)));
    ASSERT_FALSE(policy.ok()) << value << " was accepted as an attempt budget";
    EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
  }
}

/// game_features.request_id is a foreign key onto indexing_requests(id), so a
/// request has to outlive the games it produced.
TEST(RetentionPolicy, RequestsMustOutliveTheirGames) {
  const auto policy =
      LoadRetentionPolicy(PolicyFile("short_request", PolicyWith("request_seconds", "604800")));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
}

/// One asks whether the owner is still there, the other whether anyone is
/// serving this at all. A lease grown to the staleness cutoff makes them the
/// same question.
TEST(RetentionPolicy, TheLeaseMustSitBelowTheStalenessWindow) {
  const auto policy =
      LoadRetentionPolicy(PolicyFile("long_lease", PolicyWith("lease_seconds", "3600")));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
}

/// A ceiling below the staleness window cuts runs short of the very window
/// used to decide whether anything is happening at all.
TEST(RetentionPolicy, TheRunCeilingMustSitAboveTheStalenessWindow) {
  const auto policy =
      LoadRetentionPolicy(PolicyFile("low_ceiling", PolicyWith("max_run_seconds", "600")));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
}

/// Four renewals inside a lease, so three consecutive missed beats do not cost
/// a healthy worker the range it is still working on.
TEST(RetentionPolicy, TheRenewalIntervalMustLeaveRoomForMissedBeats) {
  const auto policy =
      LoadRetentionPolicy(PolicyFile("slow_renewal", PolicyWith("lease_renewal_seconds", "150")));
  ASSERT_FALSE(policy.ok());
  EXPECT_EQ(policy.status().code(), absl::StatusCode::kInvalidArgument);
}

/// Exactly four fits. The boundary is the documented ratio, so it has to be
/// the accepted side of the check rather than one step past it.
TEST(RetentionPolicy, ExactlyFourRenewalsIsAllowed) {
  const auto policy =
      LoadRetentionPolicy(PolicyFile("exact_renewal", PolicyWith("lease_renewal_seconds", "75")));
  ASSERT_TRUE(policy.ok()) << policy.status();
  EXPECT_EQ(policy->lease_renewal * 4, policy->lease);
}

/// The container has no working directory to speak of and the binary is not on
/// a path anyone spells out, so the policy is found relative to argv[0] — the
/// runfiles tree pkg_tar ships beside it.
///
/// The argv[0] here is the real one: bazel/rules/oci.bzl builds this worker
/// through linux_amd64_oci_binary, which sets entrypoint = ["/one_d4_worker"].
/// (/app is the *Java* images' convention, from linux_oci_java's package_dir —
/// using it here would illustrate a layout this binary never sees.)
TEST(RetentionPolicy, ThePathIsDerivedFromTheBinary) {
  EXPECT_EQ(RetentionPolicyPath("/one_d4_worker"),
            "/one_d4_worker.runfiles/_main/domains/games/apis/one_d4/retention_policy.json");
}

/// argv[0] is the only input. There was an ONE_D4_RETENTION_POLICY override;
/// it is gone, and this is what keeps it gone — a deployment that could point
/// a worker at its own file would be running windows no test has seen, while
/// the Java service, which never had an equivalent, went on quoting users the
/// numbers in the shipped one.
TEST(RetentionPolicy, TheEnvironmentDoesNotChangeThePath) {
  const std::string expected =
      "/one_d4_worker.runfiles/_main/domains/games/apis/one_d4/retention_policy.json";
  for (const char* name : {"ONE_D4_RETENTION_POLICY", "RETENTION_POLICY", "ONE_D4_POLICY"}) {
    ::setenv(name, "/etc/one_d4/policy.json", /*overwrite=*/1);
    EXPECT_EQ(RetentionPolicyPath("/one_d4_worker"), expected) << name << " changed the path";
    ::unsetenv(name);
  }
}

}  // namespace
}  // namespace one_d4_worker
