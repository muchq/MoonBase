# LRU Cache

A fixed-capacity Least Recently Used (LRU) cache implementation.

## Overview

This cache automatically evicts the least recently accessed items when capacity is reached. Useful for caching expensive computations, database results, or any data with temporal locality.

Inspired by [boost.compute's LRU cache](https://github.com/boostorg/compute/blob/master/include/boost/compute/detail/lru_cache.hpp).

## Features

- **O(1) operations**: Constant-time get, insert, and eviction
- **Automatic eviction**: Removes least recently used items when full
- **Simple API**: `get()`, `insert()`, `contains()`, `clear()`

## Usage

```cpp
#include "cpp/futility/cache/lru_cache.h"

using namespace futility::cache;

// Create cache with capacity of 1000 items
LRUCache<std::string, ExpensiveResult> cache(1000);

// Try cache first
auto cached = cache.get("key");
if (cached.has_value()) {
    return *cached;  // Cache hit
}

// Cache miss - compute and store
auto result = ComputeExpensive("key");
cache.insert("key", result);
return result;
```

## API Reference

| Method | Description |
|--------|-------------|
| `LRUCache(size_t capacity)` | Construct with max capacity |
| `get(key)` | Get value (updates recency), returns `std::optional` |
| `insert(key, value)` | Insert (evicts LRU if full), no-op if key exists |
| `contains(key)` | Check existence (does NOT update recency) |
| `size()` | Current number of items |
| `capacity()` | Maximum capacity |
| `empty()` | True if cache is empty |
| `clear()` | Remove all items |

## Thread Safety

This implementation is **thread-safe**. It uses `std::shared_mutex` internally to allow concurrent read operations while serializing writes.

| Method | Lock Type | Reason |
|--------|-----------|--------|
| `size()` | shared | Read-only |
| `capacity()` | none | Reads constant member |
| `empty()` | shared | Read-only |
| `contains()` | shared | Read-only, doesn't update recency |
| `get()` | unique | Mutates recency list |
| `insert()` | unique | Mutates both containers |
| `clear()` | unique | Mutates both containers |

## Bazel Target

```python
deps = ["//cpp/futility/cache:lru_cache"]
```

## License

[Boost Software License 1.0](http://www.boost.org/LICENSE_1_0.txt)