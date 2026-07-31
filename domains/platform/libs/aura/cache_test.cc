// What aura adds over the futility cache primitive is the standard
// cache_hits_total / cache_misses_total{service_name, cache} family, so
// these tests are mostly about the counters: that both outcomes are counted,
// that each is counted as itself and not as the other, and that the labels
// are the configured ones. The storage behavior belongs to the primitive and
// is pinned in //domains/platform/libs/futility/cache:cache_test; only
// delegation is checked here.

#include "domains/platform/libs/aura/cache.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace {

using futility::otel::CapturingMetricsRecorder;

// The names as recorded. The OTLP exporter appends _total, so recording
// "cache_hits_total" here would reach Prometheus as cache_hits_total_total.
constexpr const char* kHits = "cache_hits";
constexpr const char* kMisses = "cache_misses";

class AuraCacheTest : public ::testing::Test {
 protected:
  std::map<std::string, std::string> Labels(const std::string& cache) const {
    return {{"service_name", "portrait"}, {"cache", cache}};
  }

  aura::Cache<int, std::string> MakeCache(std::size_t capacity,
                                          const std::string& cache_name = "trace") {
    return aura::Cache<int, std::string>({.service_name = "portrait", .cache = cache_name},
                                         capacity, metrics_);
  }

  std::shared_ptr<CapturingMetricsRecorder> metrics_ =
      std::make_shared<CapturingMetricsRecorder>("portrait");
};

TEST_F(AuraCacheTest, AHitIsCountedAsAHitAndNotAsAMiss) {
  aura::Cache<int, std::string> cache = MakeCache(4);
  cache.insert(1, "one");

  EXPECT_EQ(cache.get(1), "one");

  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("trace")), 1);
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("trace")), 0) << "a hit also counted as a miss";
}

TEST_F(AuraCacheTest, AMissIsCountedAsAMissAndNotAsAHit) {
  aura::Cache<int, std::string> cache = MakeCache(4);

  EXPECT_FALSE(cache.get(99).has_value());

  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("trace")), 1);
  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("trace")), 0) << "a miss also counted as a hit";
}

TEST_F(AuraCacheTest, EveryLookupIsCountedExactlyOnce) {
  aura::Cache<int, std::string> cache = MakeCache(4);
  cache.insert(1, "one");

  for (int i = 0; i < 3; ++i) {
    (void)cache.get(1);
  }
  for (int i = 0; i < 2; ++i) {
    (void)cache.get(2);
  }

  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("trace")), 3);
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("trace")), 2);
}

TEST_F(AuraCacheTest, CountersCarryTheConfiguredLabels) {
  aura::Cache<int, std::string> cache = MakeCache(4);
  (void)cache.get(1);

  // CounterTotal matches attributes exactly, so these are the assertions
  // that give the one above its meaning: a counter emitted bare, or under
  // some other cache's label, is not this cache's counter.
  EXPECT_EQ(metrics_->CounterTotal(kMisses, {}), 0) << "recorded without labels";
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("some-other-cache")), 0);
  EXPECT_EQ(metrics_->CounterTotal(kMisses, {{"cache", "trace"}}), 0)
      << "recorded without a service_name";
}

TEST_F(AuraCacheTest, TheRecordedNamesDoNotCarryTheExporterSuffix) {
  aura::Cache<int, std::string> cache = MakeCache(4);
  cache.insert(1, "one");
  (void)cache.get(1);
  (void)cache.get(2);

  // The exporter appends _total. Recording it here too would land as
  // cache_hits_total_total and silently miss every dashboard query.
  EXPECT_EQ(metrics_->CounterTotal("cache_hits_total", Labels("trace")), 0);
  EXPECT_EQ(metrics_->CounterTotal("cache_misses_total", Labels("trace")), 0);
}

TEST_F(AuraCacheTest, TwoCachesInOneServiceCountSeparately) {
  aura::Cache<int, std::string> scenes = MakeCache(4, "trace");
  aura::Cache<int, std::string> thumbnails = MakeCache(4, "thumbnail");
  scenes.insert(1, "one");

  (void)scenes.get(1);      // hit on "trace"
  (void)thumbnails.get(1);  // miss on "thumbnail"

  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("trace")), 1);
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("trace")), 0);
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("thumbnail")), 1);
  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("thumbnail")), 0);
}

TEST_F(AuraCacheTest, StorageBehaviorIsThePrimitivesAndStillReachable) {
  aura::Cache<int, std::string> cache = MakeCache(2);
  cache.insert(1, "one");
  cache.insert(2, "two");

  EXPECT_EQ(cache.size(), 2u);
  EXPECT_EQ(cache.capacity(), 2u);
  EXPECT_FALSE(cache.empty());

  // Reading 1 promotes it, so inserting a third entry evicts 2.
  EXPECT_EQ(cache.get(1), "one");
  cache.insert(3, "three");

  EXPECT_EQ(cache.get(1), "one");
  EXPECT_FALSE(cache.get(2).has_value());
  EXPECT_EQ(cache.get(3), "three");
}

TEST_F(AuraCacheTest, ACacheWithNoCapacityStillReportsItsMisses) {
  aura::Cache<int, std::string> cache = MakeCache(0);
  cache.insert(1, "one");

  EXPECT_TRUE(cache.empty());
  EXPECT_FALSE(cache.get(1).has_value());

  // A cache turned off by capacity reads as a 0% hit rate, which is a fact
  // about the service; an absent metric reads as a broken exporter.
  EXPECT_EQ(metrics_->CounterTotal(kMisses, Labels("trace")), 1);
  EXPECT_EQ(metrics_->CounterTotal(kHits, Labels("trace")), 0);
}

}  // namespace
