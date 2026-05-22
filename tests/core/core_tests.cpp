#include <winelement/core.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace winelement::core;

TEST(CoreTests, LruCacheKeepsMostRecentValuesAndEvictsLeastRecent) {
    LruCache<int, std::string> cache(2U);

    cache.put(1, "one");
    cache.put(2, "two");
    ASSERT_NE(cache.get(1), nullptr);
    EXPECT_EQ(*cache.get(1), "one");

    cache.put(3, "three");

    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    ASSERT_NE(cache.get(3), nullptr);
    EXPECT_EQ(*cache.get(3), "three");
    EXPECT_EQ(cache.size(), 2U);
}

TEST(CoreTests, LruCacheCapacityResizeTrimsOldEntries) {
    LruCache<int, int> cache(4U);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);
    cache.put(4, 40);

    ASSERT_NE(cache.get(2), nullptr);
    cache.set_capacity(2U);

    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(4));
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.contains(3));
    EXPECT_EQ(cache.capacity(), 2U);
    EXPECT_EQ(cache.size(), 2U);
}

} // namespace
