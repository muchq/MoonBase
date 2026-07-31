# LRU Cache

A fixed-capacity Least Recently Used (LRU) cache implementation.

## Overview

This cache automatically evicts the least recently accessed items when capacity is reached. Useful for caching expensive computations, database results, or any data with temporal locality.

Inspired by [boost.compute's LRU cache](https://github.com/boostorg/compute/blob/master/include/boost/compute/detail/lru_cache.hpp).

## Features

- **O(1) operations**: Constant-time get, insert, and eviction
- **Automatic eviction**: Removes least recently used items when full
- **Simple API**: `get()`, `insert()`, `contains()`, `clear()`

## Which one do I want?

This is the bare primitive. A **service** almost always wants
[`aura::Cache`](../../aura/cache.h) instead, which wraps this and emits the
standard `cache_hits_total` / `cache_misses_total{service_name, cache}`
family, so its hit rate shows up on the dashboard without any per-service
metric code. Reach for the primitive directly only where there is nothing to
report to — inside another library, or in a tool.

## Usage

```cpp
#include "domains/platform/libs/futility/cache/lru_cache.h"

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
| `LRUCache(size_t capacity)` | Construct with max capacity. Capacity 0 stores nothing |
| `get(key)` | Get value (makes it most recently used), returns `std::optional` |
| `insert(key, value)` | Insert (evicts LRU if full), no-op if key exists |
| `contains(key)` | Check existence (does NOT update recency) |
| `size()` | Current number of items |
| `capacity()` | Maximum capacity |
| `empty()` | True if cache is empty |
| `clear()` | Remove all items |

There is deliberately no way to remove a single key or to replace the value
stored under one: `insert` on a key already present keeps the stored value and
does not refresh its recency. Values are expected to be a pure function of the
key, so a second insert has nothing new to say.

## Exception safety

`get` moves the existing recency node rather than rebuilding it, so the
promotion itself allocates nothing and cannot fail. (Copying the value into the
returned `std::optional` still can — but that happens after the relink, with
both containers already consistent.)

`insert` stages its allocations and commits them with a non-throwing splice, so
an allocation failure leaves the cache exactly as it was: nothing evicted, no
entry half-added. Eviction runs after that commit point, because erasing by key
runs the key's hash and equality and so is not itself non-throwing; a throw
from there leaves the cache consistent but holding one entry over capacity, for
good. That overshoot is deliberate rather than drained — see the comment on
`insert`, since looping the eviction to reclaim it would also hide the symptom
that makes an inconsistent cache detectable at all.

This matters where the values are large enough that allocation failure is a
real path rather than a theoretical one
([#1271](https://github.com/muchq/MoonBase/issues/1271)).

## Thread Safety

This implementation is **thread-safe**. It uses `std::shared_mutex` internally: the observers below share the lock, while anything that mutates state serializes. Note that `get()` is a writer — it updates recency — so concurrent `get()`s on the same cache do not proceed in parallel. `contains()` is the read-only lookup.

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
deps = ["//domains/platform/libs/futility/cache"]
```

## License

[Boost Software License 1.0](http://www.boost.org/LICENSE_1_0.txt)