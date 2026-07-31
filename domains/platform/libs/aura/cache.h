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

/// A fixed-capacity LRU cache that counts hits and misses.
///
/// Storage behavior is entirely futility's; what aura adds is that *using*
/// this type emits the standard `cache_hits_total` /
/// `cache_misses_total{service_name, cache}` family. A service gets cache
/// observability by picking this type rather than by remembering to count in
/// every branch of its own lookup code.
///
/// Counter names are recorded without the `_total` suffix; the OTLP
/// exporter appends it, as it does for `http_server_requests`.
///
/// Thread-safe, because the underlying primitive is.
template <class Key, class Value>
class Cache {
 public:
  /// @param cache_name Value of the `cache` label, distinguishing this cache
  ///        from the service's others. Static and low-cardinality.
  /// @param capacity Maximum entries. Zero stores nothing, which turns the
  ///        cache off without disturbing its call sites — the counters keep
  ///        reporting, so a disabled cache reads as a 0% hit rate rather
  ///        than as an absent metric.
  /// @param metrics Where the counters go. The `service_name` label comes
  ///        from this recorder rather than from a second argument, so a
  ///        cache series cannot end up labeled for a different service than
  ///        the meter it was recorded through.
  Cache(std::string cache_name, std::size_t capacity,
        std::shared_ptr<futility::otel::MetricsRecorder> metrics)
      : cache_(capacity),
        metrics_(std::move(metrics)),
        labels_{{"service_name", metrics_->service_name()}, {"cache", std::move(cache_name)}} {}

  /// Looks up a key, counting the outcome, and promotes it on a hit.
  std::optional<Value> get(const Key& key) {
    std::optional<Value> found = cache_.get(key);
    metrics_->RecordCounter(found.has_value() ? "cache_hits" : "cache_misses", 1, labels_);
    return found;
  }

  /// Stores a value, evicting the least recently used entry if full. A key
  /// already present keeps its stored value.
  void insert(const Key& key, const Value& value) { cache_.insert(key, value); }

 private:
  futility::cache::LRUCache<Key, Value> cache_;
  std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const std::map<std::string, std::string> labels_;
};

}  // namespace aura

#endif
