#include "domains/r3dr/apis/r3dr_v2/shortener.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace r3dr_v2 {
namespace {

constexpr absl::Time kNow = absl::FromUnixMillis(1755000000000);  // 2025-08-12T12:00:00Z

// --- ResolveExpiry: the clock-dependent rules, both sides of every line ---

TEST(ResolveExpiryTest, AFutureExpiryInsideTheWindowIsKept) {
  const int64_t requested = absl::ToUnixMillis(kNow + absl::Hours(24));
  const auto resolved = ResolveExpiry(requested, kNow);
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(*resolved, absl::FromUnixMillis(requested));
}

TEST(ResolveExpiryTest, ThePastIsRefused) {
  const auto refused = ResolveExpiry(absl::ToUnixMillis(kNow - absl::Seconds(1)), kNow);
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInvalidArgument);
}

// A link that expires the instant it is minted never resolves.
TEST(ResolveExpiryTest, ExactlyNowIsRefused) {
  EXPECT_EQ(ResolveExpiry(absl::ToUnixMillis(kNow), kNow).status().code(),
            absl::StatusCode::kInvalidArgument);
}

// Both sides of the line, so the constant means what the message says.
TEST(ResolveExpiryTest, ThirtyDaysExactlyIsTheCeiling) {
  const auto atCeiling = ResolveExpiry(absl::ToUnixMillis(kNow + absl::Hours(24 * 30)), kNow);
  ASSERT_TRUE(atCeiling.ok());

  const auto past =
      ResolveExpiry(absl::ToUnixMillis(kNow + absl::Hours(24 * 30) + absl::Milliseconds(1)), kNow);
  EXPECT_EQ(past.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(std::string(past.status().message()).find("30 days"), std::string::npos)
      << "the refusal names the actual limit: " << past.status().message();
}

// --- Shortener: cache discipline over a scripted store ---

class FakeStore final : public UrlStore {
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
      return absl::UnavailableError("postgres is down");
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

class ShortenerTest : public testing::Test {
 protected:
  ShortenerTest()
      : store_(std::make_shared<FakeStore>()),
        cache_(std::make_shared<Shortener::Cache>(
            "url_cache", 100,
            std::make_shared<futility::otel::CapturingMetricsRecorder>("r3dr_v2_test"))),
        shortener_(store_, cache_, [this] { return now_; }) {}

  std::shared_ptr<FakeStore> store_;
  std::shared_ptr<Shortener::Cache> cache_;
  Shortener shortener_;
  absl::Time now_ = kNow;
};

TEST_F(ShortenerTest, ShortenMintsAndResolveServesFromTheCache) {
  const auto slug =
      shortener_.Shorten("https://example.com/x", absl::ToUnixMillis(kNow + absl::Hours(24)));
  ASSERT_TRUE(slug.ok());
  EXPECT_EQ(*slug, "AQA");
  EXPECT_EQ(store_->last_expires_at, kNow + absl::Hours(24));

  const auto resolved = shortener_.Resolve("AQA");
  ASSERT_TRUE(resolved.ok());
  ASSERT_TRUE(resolved->has_value());
  EXPECT_EQ(**resolved, "https://example.com/x");
  // Served from the mint-time cache entry: the store's read path never ran.
  EXPECT_EQ(store_->lookups, 0);
}

TEST_F(ShortenerTest, ResolveCachesAStoreHit) {
  store_->targets["DAA"] = Target{"https://example.com/y", kNow + absl::Hours(1)};

  ASSERT_TRUE(shortener_.Resolve("DAA").ok());
  const auto second = shortener_.Resolve("DAA");
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(**second, "https://example.com/y");
  EXPECT_EQ(store_->lookups, 1) << "the second resolve is a cache hit";
}

// The expiry rides in the cache entry, so a hot slug goes dark on time.
TEST_F(ShortenerTest, ACachedSlugStopsResolvingAtItsExpiry) {
  const auto slug =
      shortener_.Shorten("https://example.com/z", absl::ToUnixMillis(kNow + absl::Hours(1)));
  ASSERT_TRUE(slug.ok());

  now_ = kNow + absl::Hours(1);  // exactly the expiry: expired, not expiring
  const auto resolved = shortener_.Resolve(*slug);
  ASSERT_TRUE(resolved.ok());
  EXPECT_FALSE(resolved->has_value());
  EXPECT_EQ(store_->lookups, 0)
      << "expiry is monotonic, so the cache answers 'gone' without a store trip";
}

TEST_F(ShortenerTest, UnknownSlugsAreNotFoundAndNotCached) {
  const auto first = shortener_.Resolve("zzz");
  ASSERT_TRUE(first.ok());
  EXPECT_FALSE(first->has_value());

  // Deliberately no negative caching (see the class comment).
  ASSERT_TRUE(shortener_.Resolve("zzz").ok());
  EXPECT_EQ(store_->lookups, 2);
}

// Anything under 3 chars cannot be a slug; answer without the store.
TEST_F(ShortenerTest, ImpossiblyShortSlugsNeverReachTheStore) {
  for (const std::string slug : {"", "a", "ab"}) {
    const auto resolved = shortener_.Resolve(slug);
    ASSERT_TRUE(resolved.ok());
    EXPECT_FALSE(resolved->has_value());
  }
  EXPECT_EQ(store_->lookups, 0);
}

// A store failure is a failure, not a miss.
TEST_F(ShortenerTest, AStoreFailureSurfacesAsAFailure) {
  store_->fail_lookups = true;
  EXPECT_EQ(shortener_.Resolve("DAA").status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(ShortenerTest, ARejectedExpiryNeverReachesTheStore) {
  const auto refused =
      shortener_.Shorten("https://example.com/x", absl::ToUnixMillis(kNow - absl::Hours(1)));
  EXPECT_EQ(refused.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(store_->inserts, 0);
}

}  // namespace
}  // namespace r3dr_v2
