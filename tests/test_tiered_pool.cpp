#include <gtest/gtest.h>
#include "impl/tiered_memory_pool.hpp"

using namespace impl;

// Test data structure
struct TestObject {
    uint64_t id;
    int32_t value;

    TestObject(uint64_t i = 0, int32_t v = 0) : id(i), value(v) {}
};

TEST(TieredMemoryPool, BasicAllocation) {
    constexpr size_t POOL_SIZE = 4;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(2);  // 1 hot + 2 cold tiers

    auto* obj1 = pool.allocate(1, 100);
    auto* obj2 = pool.allocate(2, 200);

    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    EXPECT_EQ(obj1->id, 1);
    EXPECT_EQ(obj2->id, 2);
}

TEST(TieredMemoryPool, HotTierExhaustion) {
    constexpr size_t POOL_SIZE = 4;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(2);  // 1 hot + 2 cold tiers

    // Fill hot tier (4 objects)
    TestObject* objs[POOL_SIZE * 3];
    for (size_t i = 0; i < POOL_SIZE * 3; ++i) {
        objs[i] = pool.allocate(i, static_cast<int32_t>(i * 10));
        ASSERT_NE(objs[i], nullptr) << "Failed at allocation " << i;
    }

    // Verify all objects have correct values
    for (size_t i = 0; i < POOL_SIZE * 3; ++i) {
        EXPECT_EQ(objs[i]->id, i);
        EXPECT_EQ(objs[i]->value, static_cast<int32_t>(i * 10));
    }
}

TEST(TieredMemoryPool, FullExhaustion) {
    constexpr size_t POOL_SIZE = 4;
    constexpr size_t NUM_TIERS = 2;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(NUM_TIERS);  // 1 hot + 2 cold = 3 tiers total

    // Allocate all available objects (3 tiers * 4 = 12)
    TestObject* objs[POOL_SIZE * (NUM_TIERS + 1)];
    for (size_t i = 0; i < POOL_SIZE * (NUM_TIERS + 1); ++i) {
        objs[i] = pool.allocate(i, 0);
        ASSERT_NE(objs[i], nullptr) << "Should succeed at " << i;
    }

    // Next allocation should fail
    auto* fail = pool.allocate(999, 0);
    EXPECT_EQ(fail, nullptr) << "Should return nullptr when all tiers exhausted";
}

TEST(TieredMemoryPool, Deallocation) {
    constexpr size_t POOL_SIZE = 4;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(1);  // 1 hot + 1 cold tier

    auto* obj1 = pool.allocate(1, 100);
    auto* obj2 = pool.allocate(2, 200);

    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);

    pool.deallocate(obj1);

    // Should be able to allocate again after deallocation
    auto* obj3 = pool.allocate(3, 300);
    ASSERT_NE(obj3, nullptr);
    EXPECT_EQ(obj3->id, 3);
}

TEST(TieredMemoryPool, FreeCount) {
    constexpr size_t POOL_SIZE = 100;
    constexpr size_t NUM_TIERS = 3;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(NUM_TIERS);

    // Initially only hot tier is active
    size_t initial_free = pool.freeCount();
    EXPECT_EQ(initial_free, POOL_SIZE);

    auto* obj = pool.allocate(1, 100);
    EXPECT_EQ(pool.freeCount(), initial_free - 1);

    pool.deallocate(obj);
    EXPECT_EQ(pool.freeCount(), initial_free);
}

TEST(TieredMemoryPool, Capacity) {
    constexpr size_t POOL_SIZE = 64;
    constexpr size_t NUM_TIERS = 7;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(NUM_TIERS);

    EXPECT_EQ(pool.capacity(), POOL_SIZE * (NUM_TIERS + 1));
}

TEST(TieredMemoryPool, ActiveTierCount) {
    constexpr size_t POOL_SIZE = 4;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(2);

    // Initially only hot tier is active
    EXPECT_EQ(pool.activeTierCount(), 1);

    // Allocate more than hot tier can hold
    TestObject* objs[POOL_SIZE * 2];
    for (size_t i = 0; i < POOL_SIZE * 2; ++i) {
        objs[i] = pool.allocate(i, 0);
    }

    // Now at least 2 tiers should be active
    EXPECT_GE(pool.activeTierCount(), 2);
}

TEST(TieredMemoryPool, Contains) {
    constexpr size_t POOL_SIZE = 4;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(1);

    auto* obj = pool.allocate(1, 100);
    ASSERT_NE(obj, nullptr);

    EXPECT_TRUE(pool.contains(obj));

    TestObject fake;
    EXPECT_FALSE(pool.contains(&fake));
}

// Performance test: verify L0 (hot tier) gets most allocations
TEST(TieredMemoryPool, HotTierPreference) {
    constexpr size_t POOL_SIZE = 1000;
    TieredMemoryPool<TestObject, POOL_SIZE> pool(4);

    // Allocate and deallocate within hot tier capacity
    TestObject* objs[POOL_SIZE / 2];
    for (size_t i = 0; i < POOL_SIZE / 2; ++i) {
        objs[i] = pool.allocate(i, 0);
    }

    // Only hot tier should be active
    EXPECT_EQ(pool.activeTierCount(), 1);

    // Clean up
    for (size_t i = 0; i < POOL_SIZE / 2; ++i) {
        pool.deallocate(objs[i]);
    }
}
