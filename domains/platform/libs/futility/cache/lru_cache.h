#ifndef DOMAINS_API_PLATFORM_LIBS_FUTILITY_CACHE_LRU_CACHE_H
#define DOMAINS_API_PLATFORM_LIBS_FUTILITY_CACHE_LRU_CACHE_H

/// @file lru_cache.h
/// @brief Fixed-capacity LRU (Least Recently Used) cache.
///
/// This cache automatically evicts the least recently accessed items when
/// capacity is reached. Useful for caching expensive computations, database
/// results, or any key-value data with temporal locality.
///
/// Inspired by boost.compute's LRU cache implementation.
///
/// Example usage:
/// @code
///   LRUCache<std::string, ExpensiveResult> cache(1000);  // Cache up to 1000 items
///
///   // Try to get from cache first
///   auto cached = cache.get("key");
///   if (cached.has_value()) {
///     return *cached;  // Cache hit
///   }
///
///   // Cache miss - compute and store
///   auto result = computeExpensiveResult("key");
///   cache.insert("key", result);
///   return result;
/// @endcode
///
/// @note This implementation is thread-safe. It uses a shared_mutex to allow
///       concurrent reads while serializing writes.

#include <list>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace futility::cache {

/// @brief Fixed-capacity LRU (Least Recently Used) cache.
///
/// Maintains a bounded cache that automatically evicts the least recently
/// used items when capacity is exceeded. Provides O(1) average-case lookup,
/// insertion, and eviction.
///
/// @tparam Key The key type. Must be hashable (usable as unordered_map key).
/// @tparam Value The cached value type.
template <class Key, class Value>
class LRUCache {
 public:
  typedef Key key_type;
  typedef Value value_type;
  typedef std::list<key_type> list_type;
  typedef std::unordered_map<key_type, std::pair<value_type, typename list_type::iterator>>
      map_type;

  /// @brief Constructs an LRU cache with the given capacity.
  /// @param capacity Maximum number of items the cache can hold.
  explicit LRUCache(size_t capacity) : m_capacity(capacity) {}

  ~LRUCache() = default;

  /// @brief Returns the current number of items in the cache.
  size_t size() const {
    std::shared_lock lock(mutex_);
    return m_map.size();
  }

  /// @brief Returns the maximum capacity of the cache.
  size_t capacity() const { return m_capacity; }

  /// @brief Returns true if the cache is empty.
  bool empty() const {
    std::shared_lock lock(mutex_);
    return m_map.empty();
  }

  /// @brief Checks if a key exists in the cache.
  /// @param key The key to look up.
  /// @return true if the key is present, false otherwise.
  /// @note Does NOT update the key's recency (unlike get()).
  bool contains(const key_type& key) const {
    std::shared_lock lock(mutex_);
    return m_map.find(key) != m_map.end();
  }

  /// @brief Inserts a key-value pair into the cache.
  ///
  /// If the key already exists, this is a no-op: the stored value is kept and
  /// its recency is *not* refreshed. If the cache is at capacity, the least
  /// recently used item is evicted to make room.
  ///
  /// A cache built with capacity 0 stores nothing, and every later get() is a
  /// miss.
  ///
  /// @param key The key to insert.
  /// @param value The value to associate with the key.
  /// @note There is no way to replace the value stored under an existing key.
  ///       Values here are expected to be a pure function of the key, so a
  ///       second insert has nothing new to say.
  void insert(const key_type& key, const value_type& value) {
    std::unique_lock lock(mutex_);
    if (m_capacity == 0 || m_map.contains(key)) {
      return;
    }

    // Stage the recency node in a list of its own. Both allocations below
    // can throw, and until the splice neither container has been touched —
    // so a failure unwinds `staged` and leaves the cache exactly as it was,
    // rather than half-updated. splice does not allocate and cannot throw,
    // which is what makes it the commit point.
    list_type staged;
    staged.push_front(key);
    m_map.emplace(key, std::make_pair(value, staged.begin()));
    m_list.splice(m_list.begin(), staged);

    // Only now evict, because eviction is not a non-throwing operation: it
    // erases by key, which runs the key's hash and equality. Evicting while
    // the map still pointed into `staged` meant a throw from either left a
    // map entry referring to a node that unwinding was about to free. The
    // containers agree from the splice onward, so the worst a throw here can
    // do is leave the cache one entry over capacity, permanently but
    // boundedly, since each later insert still evicts one.
    //
    // Deliberately an `if` and not a `while`. A loop would drain that
    // overshoot — but it would equally drain a recency node that no map
    // entry points at, and that is the visible symptom of every way the two
    // containers can fall out of step. Measured, not assumed: with a loop
    // here, deleting the duplicate-key guard above survives the whole suite,
    // including the 20-thread stress test that otherwise catches it at 98
    // entries in a cache of 50. A bounded overshoot after an
    // all-but-unreachable throw is worth less than keeping that class of bug
    // loud.
    if (m_map.size() > m_capacity) {
      evict();
    }
  }

  /// @brief Retrieves a value from the cache.
  ///
  /// If the key exists, it becomes the most recently used item.
  ///
  /// @param key The key to look up.
  /// @return The cached value if found, std::nullopt otherwise.
  std::optional<value_type> get(const key_type& key) {
    std::unique_lock lock(mutex_);
    const typename map_type::iterator i = m_map.find(key);
    if (i == m_map.end()) {
      return std::nullopt;
    }

    // splice relinks the existing node: no allocation, no copy of the key,
    // and the stored iterator stays valid and keeps pointing at the same
    // element. Splicing an element in front of itself is defined as a no-op,
    // so the already-at-front case needs no branch of its own.
    //
    // Copying the value into the returned optional still can throw — that is
    // the whole point of #1271, the values here are large. What matters is
    // that it throws *after* the relink, with both containers already
    // consistent, so there is no window where the map holds an iterator into
    // a node the list has freed.
    m_list.splice(m_list.begin(), m_list, i->second.second);
    return i->second.first;
  }

  /// @brief Removes all items from the cache.
  void clear() {
    std::unique_lock lock(mutex_);
    m_map.clear();
    m_list.clear();
  }

 private:
  /// @brief Evicts the least recently used item from the cache.
  ///
  /// Only called with the list non-empty: insert() returns early at capacity
  /// 0, so an over-capacity map always has at least one node to drop.
  void evict() {
    typename list_type::iterator i = --m_list.end();
    m_map.erase(*i);
    m_list.erase(i);
  }

  map_type m_map;                    ///< Hash map for O(1) key lookup
  list_type m_list;                  ///< Doubly-linked list for recency ordering
  const size_t m_capacity;           ///< Maximum cache capacity
  mutable std::shared_mutex mutex_;  ///< Mutex for thread-safe access
};

}  // namespace futility::cache

#endif
