#ifndef DOMAINS_PLATFORM_LIBS_AURA_CACHE_H
#define DOMAINS_PLATFORM_LIBS_AURA_CACHE_H

/// @file cache.h
/// @brief An LRU cache that reports its own hit rate.

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "domains/platform/libs/futility/cache/lru_cache.h"
#include "domains/platform/libs/futility/otel/metrics.h"

namespace aura {

/// Identifies one cache within one service — the label pair every cache
/// counter carries.
///
/// Both members are static, low-cardinality strings, so a service with
/// several caches gets one series per cache without cardinality risk. They
/// are grouped into a type of their own so a call site names them
/// (`{.service_name = "portrait", .cache = "trace"}`) instead of passing two
/// interchangeable strings in an order only the header remembers.
struct CacheId {
  /// Matches the service_name label on the service's other instruments.
  std::string service_name;
  /// Distinguishes this cache from the service's others.
  std::string cache;
};

/// A fixed-capacity LRU cache that counts hits and misses.
///
/// Wraps futility's cache primitive the way the serving chain wraps
/// futility's rate-limiter primitive: the storage behavior is entirely the
/// primitive's, and what aura adds is that *using* it emits the standard
/// `cache_hits_total` / `cache_misses_total{service_name, cache}` family.
/// A service gets cache observability by picking this type rather than by
/// remembering to count in every branch of its own lookup code.
///
/// Counter names are recorded without the `_total` suffix; the OTLP
/// exporter appends it, as it does for `http_server_requests`.
///
/// Thread-safe, because the underlying primitive is.
template <class Key, class Value>
class Cache {
 public:
  /// @param id The service and cache labels for this instance.
  /// @param capacity Maximum entries. Zero stores nothing, which turns the
  ///        cache off without disturbing its call sites — the counters keep
  ///        reporting, so a disabled cache reads as a 0% hit rate rather
  ///        than as an absent metric.
  /// @param metrics Where the counters go; share the service's recorder so
  ///        cache series carry the same service_name as everything else.
  Cache(CacheId id, std::size_t capacity, std::shared_ptr<futility::otel::MetricsRecorder> metrics)
      : cache_(capacity),
        metrics_(std::move(metrics)),
        labels_{{"service_name", std::move(id.service_name)}, {"cache", std::move(id.cache)}} {}

  /// Looks up a key, counting the outcome, and promotes it on a hit.
  std::optional<Value> get(const Key& key) {
    std::optional<Value> found = cache_.get(key);
    metrics_->RecordCounter(found.has_value() ? "cache_hits" : "cache_misses", 1, labels_);
    return found;
  }

  /// Stores a value, evicting the least recently used entry if full. A key
  /// already present keeps its stored value.
  void insert(const Key& key, const Value& value) { cache_.insert(key, value); }

  std::size_t size() const { return cache_.size(); }
  std::size_t capacity() const { return cache_.capacity(); }
  bool empty() const { return cache_.empty(); }

 private:
  futility::cache::LRUCache<Key, Value> cache_;
  std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const std::map<std::string, std::string> labels_;
};

}  // namespace aura

#endif
