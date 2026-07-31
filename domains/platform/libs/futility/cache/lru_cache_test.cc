#include "lru_cache.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace futility::cache;

// A key that counts how often it is copied and can be armed to throw on a
// chosen future copy.
//
// Copying a key is the cache's only allocation-bearing use of one, so "how
// many copies does this operation make" and "how many chances does it have
// to fail" are the same question — which is what lets the exception-safety
// tests below be deterministic instead of depending on a real allocator
// running out of memory.
struct ProbeKey {
  int id = 0;

  ProbeKey() = default;
  explicit ProbeKey(int key_id) : id(key_id) {}

  ProbeKey(const ProbeKey& other) : id(other.id) { CountCopy(); }
  ProbeKey& operator=(const ProbeKey& other) {
    CountCopy();
    id = other.id;
    return *this;
  }

  bool operator==(const ProbeKey& other) const { return id == other.id; }

  static inline int copies = 0;
  /// The copy ordinal that throws; 0 means no copy ever throws.
  static inline int throw_on_copy = 0;

  static void Reset() {
    copies = 0;
    throw_on_copy = 0;
  }
  /// Arms the very next copy, whenever it happens, to fail.
  static void FailOnNextCopy() { throw_on_copy = copies + 1; }

 private:
  static void CountCopy() {
    if (++copies == throw_on_copy) {
      throw std::runtime_error("ProbeKey copy failed");
    }
  }
};

template <>
struct std::hash<ProbeKey> {
  std::size_t operator()(const ProbeKey& key) const noexcept { return std::hash<int>{}(key.id); }
};

TEST(LRUCache, EmptyCacheReturnsOptionalEmpty) {
  // Arrange
  LRUCache<int, std::string> cache(2);

  // Act
  auto item = cache.get(42);

  // Assert
  EXPECT_FALSE(item.has_value());
}

TEST(LRUCache, EvictionWorks) {
  // Arrange
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "hello");
  cache.insert(2, "hi");

  EXPECT_TRUE(cache.get(1).has_value());
  EXPECT_TRUE(cache.get(2).has_value());
  EXPECT_FALSE(cache.get(3).has_value());

  // should evict 1
  cache.insert(3, "sup");

  EXPECT_FALSE(cache.get(1).has_value());
  EXPECT_TRUE(cache.get(2).has_value());
  EXPECT_TRUE(cache.get(3).has_value());
}

TEST(LRUCache, ConcurrentReads) {
  LRUCache<int, std::string> cache(100);
  for (int i = 0; i < 100; ++i) {
    cache.insert(i, "value" + std::to_string(i));
  }

  std::vector<std::thread> threads;
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&cache]() {
      for (int i = 0; i < 1000; ++i) {
        cache.contains(i % 100);
        cache.size();
        cache.empty();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(cache.size(), 100);
}

TEST(LRUCache, ConcurrentInserts) {
  LRUCache<int, int> cache(1000);

  std::vector<std::thread> threads;
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&cache, t]() {
      for (int i = 0; i < 100; ++i) {
        cache.insert(t * 100 + i, i);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(cache.size(), 1000);
}

TEST(LRUCache, ConcurrentGetAndInsert) {
  LRUCache<int, int> cache(100);

  std::vector<std::thread> threads;
  for (int t = 0; t < 5; ++t) {
    // Writer threads
    threads.emplace_back([&cache, t]() {
      for (int i = 0; i < 100; ++i) {
        cache.insert(t * 100 + i, i);
      }
    });
    // Reader threads
    threads.emplace_back([&cache]() {
      for (int i = 0; i < 500; ++i) {
        cache.get(i % 200);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Cache should have exactly 100 items (its capacity)
  EXPECT_LE(cache.size(), 100);
}

TEST(LRUCache, ConcurrentAccessDoesNotCorrupt) {
  LRUCache<int, int> cache(50);

  std::vector<std::thread> threads;
  for (int t = 0; t < 20; ++t) {
    threads.emplace_back([&cache, t]() {
      for (int i = 0; i < 200; ++i) {
        int key = (t * 200 + i) % 100;
        cache.insert(key, i);
        (void)cache.get(key);
        cache.contains(key);
        cache.size();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Verify cache invariants
  EXPECT_LE(cache.size(), 50);
  EXPECT_EQ(cache.capacity(), 50);
}

TEST(LRUCache, ConcurrentEviction) {
  LRUCache<int, int> cache(10);

  std::vector<std::thread> threads;
  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&cache, t]() {
      for (int i = 0; i < 100; ++i) {
        cache.insert(t * 1000 + i, i);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Cache should never exceed capacity
  EXPECT_LE(cache.size(), 10);
}

// --- Recency semantics -------------------------------------------------
// EvictionWorks above passes whether or not get() promotes: it reads keys in
// the same order it inserted them, so the least-recently-used entry is the
// oldest either way. These make the promotion observable.

TEST(LRUCache, GetPromotesTheKeyItReturns) {
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "one");
  cache.insert(2, "two");

  // 1 is the older entry, so it is next to be evicted — until this read.
  ASSERT_TRUE(cache.get(1).has_value());
  cache.insert(3, "three");

  EXPECT_TRUE(cache.get(1).has_value()) << "the promoted key was evicted";
  EXPECT_FALSE(cache.get(2).has_value()) << "the stale key survived";
  EXPECT_TRUE(cache.get(3).has_value());
}

TEST(LRUCache, InsertOnAnExistingKeyKeepsTheStoredValue) {
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "first");
  cache.insert(1, "second");

  EXPECT_EQ(cache.size(), 1u);
  EXPECT_EQ(cache.get(1), "first");
}

TEST(LRUCache, InsertOnAnExistingKeyDoesNotPromoteIt) {
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "one");
  cache.insert(2, "two");

  // A no-op insert is a no-op for recency too, so 1 stays next in line.
  cache.insert(1, "one again");
  cache.insert(3, "three");

  EXPECT_FALSE(cache.get(1).has_value());
  EXPECT_TRUE(cache.get(2).has_value());
  EXPECT_TRUE(cache.get(3).has_value());
}

TEST(LRUCache, ContainsDoesNotPromote) {
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "one");
  cache.insert(2, "two");

  EXPECT_TRUE(cache.contains(1));
  cache.insert(3, "three");

  EXPECT_FALSE(cache.contains(1)) << "contains() promoted the key it inspected";
  EXPECT_TRUE(cache.contains(2));
}

// --- Capacity boundaries -----------------------------------------------

TEST(LRUCache, ZeroCapacityStoresNothing) {
  // Evicting to make room in an empty cache used to walk off the end of the
  // recency list.
  LRUCache<int, std::string> cache(0);

  cache.insert(1, "one");

  EXPECT_EQ(cache.size(), 0u);
  EXPECT_TRUE(cache.empty());
  EXPECT_FALSE(cache.contains(1));
  EXPECT_FALSE(cache.get(1).has_value());
}

TEST(LRUCache, CapacityOneHoldsOnlyTheMostRecentKey) {
  LRUCache<int, std::string> cache(1);

  cache.insert(1, "one");
  EXPECT_EQ(cache.get(1), "one");

  cache.insert(2, "two");
  EXPECT_EQ(cache.size(), 1u);
  EXPECT_FALSE(cache.get(1).has_value());
  EXPECT_EQ(cache.get(2), "two");
}

TEST(LRUCache, ClearResetsRecencyAsWellAsContents) {
  LRUCache<int, std::string> cache(2);
  cache.insert(1, "one");
  cache.insert(2, "two");

  cache.clear();

  EXPECT_TRUE(cache.empty());
  EXPECT_EQ(cache.size(), 0u);
  EXPECT_FALSE(cache.get(1).has_value());

  // Had clear() emptied only the map, these would queue up behind two stale
  // recency nodes and the next evictions would free nothing from the map.
  cache.insert(3, "three");
  cache.insert(4, "four");
  cache.insert(5, "five");

  EXPECT_EQ(cache.size(), 2u);
  EXPECT_FALSE(cache.get(3).has_value());
  EXPECT_TRUE(cache.get(4).has_value());
  EXPECT_TRUE(cache.get(5).has_value());
}

// --- Exception safety (#1271) ------------------------------------------

TEST(LRUCacheExceptionSafety, PromotingCopiesNoKeySoItCannotFail) {
  ProbeKey::Reset();
  LRUCache<ProbeKey, int> cache(4);
  cache.insert(ProbeKey(1), 10);
  cache.insert(ProbeKey(2), 20);

  const int copies_before = ProbeKey::copies;
  ProbeKey::FailOnNextCopy();

  // Promotion relinks the node the cache already owns. Building a fresh node
  // instead meant copying the key, which is an allocation, which is a way to
  // fail while the map still held an iterator into the node just erased.
  std::optional<int> promoted;
  EXPECT_NO_THROW(promoted = cache.get(ProbeKey(1)));
  EXPECT_EQ(promoted, 10);
  EXPECT_EQ(ProbeKey::copies, copies_before) << "promotion copied the key";

  ProbeKey::Reset();
}

TEST(LRUCacheExceptionSafety, TheCacheIsIntactAfterAnArmedPromotion) {
  ProbeKey::Reset();
  LRUCache<ProbeKey, int> cache(2);
  cache.insert(ProbeKey(1), 10);
  cache.insert(ProbeKey(2), 20);

  ProbeKey::FailOnNextCopy();
  EXPECT_NO_THROW((void)cache.get(ProbeKey(1)));
  ProbeKey::Reset();

  // A promotion that half-completed used to leave the map pointing at a
  // freed node, so coming back for the same key was undefined rather than
  // simply correct.
  EXPECT_EQ(cache.get(ProbeKey(1)), 10);
  EXPECT_EQ(cache.get(ProbeKey(2)), 20);

  // Recency still tracks reality: 1 was read first, so it goes first.
  cache.insert(ProbeKey(3), 30);
  EXPECT_EQ(cache.size(), 2u);
  EXPECT_FALSE(cache.get(ProbeKey(1)).has_value());
  EXPECT_EQ(cache.get(ProbeKey(2)), 20);
  EXPECT_EQ(cache.get(ProbeKey(3)), 30);
}

TEST(LRUCacheExceptionSafety, AFailedInsertEvictsNothing) {
  ProbeKey::Reset();
  LRUCache<ProbeKey, int> cache(2);
  cache.insert(ProbeKey(1), 10);
  cache.insert(ProbeKey(2), 20);
  ASSERT_EQ(cache.size(), 2u);

  ProbeKey::FailOnNextCopy();
  EXPECT_THROW(cache.insert(ProbeKey(3), 30), std::runtime_error);
  ProbeKey::Reset();

  // Making room before knowing the new entry can be built trades a live
  // entry for one that never arrives.
  EXPECT_EQ(cache.size(), 2u);
  EXPECT_EQ(cache.get(ProbeKey(1)), 10);
  EXPECT_EQ(cache.get(ProbeKey(2)), 20);
  EXPECT_FALSE(cache.get(ProbeKey(3)).has_value());
}

TEST(LRUCacheExceptionSafety, AFailedInsertLeavesNoOrphanedRecencyNode) {
  ProbeKey::Reset();
  // Roomy enough that the failing insert evicts nothing, isolating the
  // orphaned-node failure from the one above.
  LRUCache<ProbeKey, int> cache(4);
  cache.insert(ProbeKey(1), 10);
  cache.insert(ProbeKey(2), 20);

  // Let the recency node be built and fail while inserting the map entry
  // that is supposed to point at it: the first copy builds the node, the
  // second builds the map key.
  ProbeKey::throw_on_copy = ProbeKey::copies + 2;
  EXPECT_THROW(cache.insert(ProbeKey(3), 30), std::runtime_error);
  ProbeKey::Reset();

  ASSERT_EQ(cache.size(), 2u);

  // A node in the list that no map entry refers to is invisible until
  // eviction reaches it — and evicting it frees nothing from the map, so the
  // cache quietly grows past its capacity.
  for (int id = 4; id < 12; ++id) {
    cache.insert(ProbeKey(id), id * 10);
    EXPECT_LE(cache.size(), cache.capacity()) << "after inserting key " << id;
  }
}
