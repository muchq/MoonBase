#ifndef CPP_FUTILITY_CACHE_LRU_CACHE_H
#define CPP_FUTILITY_CACHE_LRU_CACHE_H

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
  /// If the key already exists, this is a no-op (the existing value is kept).
  /// If the cache is at capacity, the least recently used item is evicted first.
  ///
  /// @param key The key to insert.
  /// @param value The value to associate with the key.
  /// @note To update an existing key's value, remove it first or use a different pattern.
  void insert(const key_type& key, const value_type& value) {
    std::unique_lock lock(mutex_);
    if (!m_map.contains(key)) {
      // insert item into the cache, but first check if it is full
      if (m_map.size() >= m_capacity) {
        // cache is full, evict the least recently used item
        evict();
      }

      // insert the new item
      m_list.push_front(key);
      m_map[key] = std::make_pair(value, m_list.begin());
    }
  }

  /// @brief Retrieves a value from the cache.
  ///
  /// If the key exists, it is moved to the front of the recency list,
  /// making it the most recently used item.
  ///
  /// @param key The key to look up.
  /// @return The cached value if found, std::nullopt otherwise.
  std::optional<value_type> get(const key_type& key) {
    std::unique_lock lock(mutex_);
    // lookup value in the cache
    typename map_type::iterator i = m_map.find(key);
    if (i == m_map.end()) {
      // value not in cache
      return std::nullopt;
    }

    // return the value, but first update its place in the most
    // recently used list
    typename list_type::iterator j = i->second.second;
    if (j != m_list.begin()) {
      // move item to the front of the most recently used list
      m_list.erase(j);
      m_list.push_front(key);

      // update iterator in map
      j = m_list.begin();
      const value_type& value = i->second.first;
      m_map[key] = std::make_pair(value, j);

      // return the value
      return value;
    } else {
      // the item is already at the front of the most recently
      // used list so just return it
      return i->second.first;
    }
  }

  /// @brief Removes all items from the cache.
  void clear() {
    std::unique_lock lock(mutex_);
    m_map.clear();
    m_list.clear();
  }

 private:
  /// @brief Evicts the least recently used item from the cache.
  void evict() {
    // evict item from the end of most recently used list
    typename list_type::iterator i = --m_list.end();
    m_map.erase(*i);
    m_list.erase(i);
  }

  map_type m_map;                   ///< Hash map for O(1) key lookup
  list_type m_list;                 ///< Doubly-linked list for recency ordering
  size_t m_capacity;                ///< Maximum cache capacity
  mutable std::shared_mutex mutex_; ///< Mutex for thread-safe access
};

}  // namespace futility::cache

#endif
