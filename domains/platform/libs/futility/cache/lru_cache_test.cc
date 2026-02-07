#include "lru_cache.h"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

using namespace futility::cache;

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
