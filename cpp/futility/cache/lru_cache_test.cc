#include "lru_cache.h"

#include <string>
#include <gtest/gtest.h>

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
