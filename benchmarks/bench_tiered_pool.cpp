#include <benchmark/benchmark.h>
#include "impl/memory_pool.hpp"
#include "impl/tiered_memory_pool.hpp"

using namespace impl;

// Test data structure matching Order size (64 bytes)
struct TestObject {
    uint64_t order_id;
    int32_t price;
    uint32_t qty;
    uint8_t side;
    uint8_t padding[3];
    TestObject* prev;
    TestObject* next;

    TestObject(uint64_t id = 0, int32_t p = 0, uint32_t q = 0, uint8_t s = 0)
        : order_id(id), price(p), qty(q), side(s), prev(this), next(this) {}
};

constexpr size_t POOL_SIZE = 65536;  // Same as MAX_ORDERS

// ==========================================
// Benchmark: Single MemoryPool Allocation (L0 equivalent)
// ==========================================
static void BM_MemoryPool_Allocate(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;

    // Pre-allocate some objects to simulate active use
    TestObject* objs[1000];
    for (int i = 0; i < 1000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t i = 1000;
    for (auto _ : state) {
        auto* obj = pool.allocate(i++, 100, 10, 0);
        benchmark::DoNotOptimize(obj);

        // Deallocate to keep pool from filling
        if (i % 1001 == 0) {
            for (int j = 0; j < 1000; ++j) {
                pool.deallocate(objs[j]);
            }
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MemoryPool_Allocate);

// ==========================================
// Benchmark: TieredMemoryPool - Hot Tier (L0) Allocation
// ==========================================
static void BM_TieredMemoryPool_HotTier(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);  // 1 hot + 16 cold tiers

    // Pre-allocate some objects to simulate active use
    TestObject* objs[1000];
    for (int i = 0; i < 1000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t i = 1000;
    for (auto _ : state) {
        auto* obj = pool.allocate(i++, 100, 10, 0);
        benchmark::DoNotOptimize(obj);

        // Deallocate to keep pool from filling
        if (i % 1001 == 0) {
            for (int j = 0; j < 1000; ++j) {
                pool.deallocate(objs[j]);
            }
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TieredMemoryPool_HotTier);

// ==========================================
// Benchmark: TieredMemoryPool - Cold Tier Allocation (overflow)
// ==========================================
static void BM_TieredMemoryPool_ColdTier(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);

    // Fill hot tier to force cold tier allocation
    TestObject* objs[POOL_SIZE];
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t i = POOL_SIZE;
    for (auto _ : state) {
        // This allocation will go to cold tier
        auto* obj = pool.allocate(i++, 100, 10, 0);
        benchmark::DoNotOptimize(obj);

        // Deallocate to prevent exhaustion
        if ((i - POOL_SIZE) % 1001 == 0) {
            for (size_t j = 0; j < 1000; ++j) {
                pool.deallocate(objs[POOL_SIZE - 1000 + j]);
            }
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TieredMemoryPool_ColdTier);

// ==========================================
// Benchmark: Deallocation Comparison
// ==========================================
static void BM_MemoryPool_Deallocate(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;

    // Pre-allocate objects
    TestObject* objs[10000];
    for (int i = 0; i < 10000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    for (auto _ : state) {
        state.PauseTiming();
        // Re-allocate for next iteration
        for (int i = 0; i < 10000; ++i) {
            objs[i] = pool.allocate(i, 100, 10, 0);
        }
        state.ResumeTiming();

        for (int i = 0; i < 10000; ++i) {
            pool.deallocate(objs[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_MemoryPool_Deallocate);

static void BM_TieredMemoryPool_Deallocate_HotTier(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);

    // Pre-allocate objects (all in hot tier)
    TestObject* objs[10000];
    for (int i = 0; i < 10000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    for (auto _ : state) {
        state.PauseTiming();
        // Re-allocate for next iteration
        for (int i = 0; i < 10000; ++i) {
            objs[i] = pool.allocate(i, 100, 10, 0);
        }
        state.ResumeTiming();

        for (int i = 0; i < 10000; ++i) {
            pool.deallocate(objs[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_TieredMemoryPool_Deallocate_HotTier);

// ==========================================
// Benchmark: Mixed Allocate/Deallocate Workload
// ==========================================
static void BM_MemoryPool_Mixed(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;

    // Start with some allocations
    TestObject* objs[5000];
    for (int i = 0; i < 5000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t alloc_idx = 5000;
    size_t dealloc_idx = 0;

    for (auto _ : state) {
        // Allocate
        objs[alloc_idx % 5000] = pool.allocate(alloc_idx, 100, 10, 0);
        benchmark::DoNotOptimize(objs[alloc_idx % 5000]);
        alloc_idx++;

        // Deallocate
        pool.deallocate(objs[dealloc_idx % 5000]);
        dealloc_idx++;
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_MemoryPool_Mixed);

static void BM_TieredMemoryPool_Mixed(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);

    // Start with some allocations
    TestObject* objs[5000];
    for (int i = 0; i < 5000; ++i) {
        objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t alloc_idx = 5000;
    size_t dealloc_idx = 0;

    for (auto _ : state) {
        // Allocate
        objs[alloc_idx % 5000] = pool.allocate(alloc_idx, 100, 10, 0);
        benchmark::DoNotOptimize(objs[alloc_idx % 5000]);
        alloc_idx++;

        // Deallocate
        pool.deallocate(objs[dealloc_idx % 5000]);
        dealloc_idx++;
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_TieredMemoryPool_Mixed);

// ==========================================
// Benchmark: Order Book Style Pattern
// Simulates: add many orders, cancel many orders
// ==========================================
static void BM_MemoryPool_OrderBookPattern(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;

    for (auto _ : state) {
        state.PauseTiming();

        // Add 10000 orders
        TestObject* orders[10000];
        for (int i = 0; i < 10000; ++i) {
            orders[i] = pool.allocate(i, 100 + (i % 100), 10, 0);
        }

        state.ResumeTiming();

        // Cancel 5000 orders (simulate order cancellation)
        for (int i = 0; i < 5000; ++i) {
            pool.deallocate(orders[i]);
        }

        // Add 5000 new orders
        for (int i = 0; i < 5000; ++i) {
            orders[i] = pool.allocate(10000 + i, 100 + (i % 100), 10, 0);
        }

        state.PauseTiming();
        // Cleanup
        for (int i = 0; i < 10000; ++i) {
            pool.deallocate(orders[i]);
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_MemoryPool_OrderBookPattern);

static void BM_TieredMemoryPool_OrderBookPattern(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);

    for (auto _ : state) {
        state.PauseTiming();

        // Add 10000 orders
        TestObject* orders[10000];
        for (int i = 0; i < 10000; ++i) {
            orders[i] = pool.allocate(i, 100 + (i % 100), 10, 0);
        }

        state.ResumeTiming();

        // Cancel 5000 orders (simulate order cancellation)
        for (int i = 0; i < 5000; ++i) {
            pool.deallocate(orders[i]);
        }

        // Add 5000 new orders
        for (int i = 0; i < 5000; ++i) {
            orders[i] = pool.allocate(10000 + i, 100 + (i % 100), 10, 0);
        }

        state.PauseTiming();
        // Cleanup
        for (int i = 0; i < 10000; ++i) {
            pool.deallocate(orders[i]);
        }
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_TieredMemoryPool_OrderBookPattern);

// ==========================================
// Benchmark: Fill Hot Tier and Overflow
// Shows what happens when tier boundary is crossed
// ==========================================
static void BM_TieredMemoryPool_Overflow(benchmark::State& state) {
    TieredMemoryPool<TestObject, POOL_SIZE> pool(16);

    // First, fill the hot tier
    TestObject* hot_objs[POOL_SIZE];
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        hot_objs[i] = pool.allocate(i, 100, 10, 0);
    }

    size_t cold_alloc_count = 0;
    for (auto _ : state) {
        // Allocate in cold tier
        auto* obj = pool.allocate(POOL_SIZE + cold_alloc_count, 100, 10, 0);
        benchmark::DoNotOptimize(obj);
        cold_alloc_count++;

        // Deallocate some to prevent exhaustion
        if (cold_alloc_count % 1000 == 0) {
            for (size_t i = 0; i < 1000; ++i) {
                pool.deallocate(hot_objs[i]);
            }
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TieredMemoryPool_Overflow);

BENCHMARK_MAIN();
