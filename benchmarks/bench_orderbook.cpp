#include <benchmark/benchmark.h>
#include "impl/order_book.hpp"
#include "framework/define.hpp"
#include <memory>
#include <array>

using namespace impl;

// ==========================================
// Benchmark: Add Order
// ==========================================
static void BM_AddOrder(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming(); 
        auto ob_ptr = std::make_unique<OrderBook>("TEST");
        state.ResumeTiming(); 

        for (int i = 0; i < state.range(0); ++i) {
            ob_ptr->addOrder(i, 100 + (i % 10), 10, Side::BUY);
        }

        state.PauseTiming(); 
        ob_ptr.reset(); 
        state.ResumeTiming(); 
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
// Range(1000, 10000) 表示每次 batch 插入 1000 到 10000 个订单
BENCHMARK(BM_AddOrder)->Range(1000, 10000); 

// ==========================================
// Benchmark: Imbalance 
// ==========================================
static void BM_GetImbalance(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    for (int i = 0; i < 1000; ++i) {
        ob_ptr->addOrder(i * 2, 100, 10, Side::BUY);     
        ob_ptr->addOrder(i * 2 + 1, 102, 10, Side::SELL); 
    }

    for (auto _ : state) {
        double result = ob_ptr->getImbalance(10);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_GetImbalance);

// ==========================================
// Benchmark: Process Trade
// ==========================================
static void BM_ProcessTrade(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    ob_ptr->addOrder(99999, 100, 2000000000, Side::BUY);
    
    uint64_t t = 1000000000ULL;

    for (auto _ : state) {
        ob_ptr->processTrade(99999, 1, 100, 10, Side::BUY, t);
        t += 1000; 
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProcessTrade);

// ==========================================
// Benchmark: VWAP 
// ==========================================
static void BM_GetVWAP(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    uint64_t t = 1000000000ULL;
    ob_ptr->addOrder(1, 100, 1000000, Side::BUY);
    
    for(int i=0; i<2000; ++i) {
        ob_ptr->processTrade(1, i, 100, 10, Side::BUY, t + i);
    }

    for (auto _ : state) {
        uint64_t vwap = ob_ptr->getVWAP();
        benchmark::DoNotOptimize(vwap);
    }
}
BENCHMARK(BM_GetVWAP);

// ==========================================
// Benchmark: MemoryPool
// ==========================================

constexpr size_t POOL_SIZE = 65536;  // Same as MAX_ORDERS

// Test data structure for benchmarking
struct TestObject {
    uint64_t id;
    int32_t value;
    char data[64];  // Simulate larger object size

    TestObject(uint64_t i, int32_t v) : id(i), value(v) {}
    TestObject() = default;
};

// Sequential allocation and deallocation
static void BM_MemoryPool_Sequential(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;
    std::array<TestObject*, POOL_SIZE> objs{};

    for (auto _ : state) {
        state.PauseTiming();
        pool.~MemoryPool();
        new (&pool) MemoryPool<TestObject, POOL_SIZE>();
        objs.fill(nullptr);
        state.ResumeTiming();

        for (size_t i = 0; i < POOL_SIZE; ++i) {
            objs[i] = pool.allocate(i, static_cast<int32_t>(i));
        }
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            pool.deallocate(objs[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * POOL_SIZE * 2);
}

// Random allocation and deallocation (interleaved)
static void BM_MemoryPool_Random(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;
    std::array<TestObject*, POOL_SIZE> objs{};
    constexpr size_t num_ops = POOL_SIZE / 2;

    for (auto _ : state) {
        state.PauseTiming();
        pool.~MemoryPool();
        new (&pool) MemoryPool<TestObject, POOL_SIZE>();
        objs.fill(nullptr);
        state.ResumeTiming();

        for (size_t i = 0; i < num_ops; ++i) {
            objs[i] = pool.allocate(i, static_cast<int32_t>(i));
        }

        size_t alloc_idx = num_ops;
        size_t dealloc_idx = 0;
        for (size_t i = 0; i < num_ops; ++i) {
            if (alloc_idx < POOL_SIZE && (i % 3 != 0)) {
                objs[alloc_idx] = pool.allocate(alloc_idx, static_cast<int32_t>(alloc_idx));
                ++alloc_idx;
            } else if (dealloc_idx < num_ops) {
                pool.deallocate(objs[dealloc_idx]);
                objs[dealloc_idx] = nullptr;
                ++dealloc_idx;
            }
        }

        for (size_t i = 0; i < POOL_SIZE; ++i) {
            if (objs[i]) pool.deallocate(objs[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * num_ops * 3);
}

// Nearly full pool stress test
static void BM_MemoryPool_NearlyFull(benchmark::State& state) {
    MemoryPool<TestObject, POOL_SIZE> pool;
    std::array<TestObject*, POOL_SIZE> objs{};
    constexpr size_t fill_count = POOL_SIZE - 100;

    for (auto _ : state) {
        state.PauseTiming();
        pool.~MemoryPool();
        new (&pool) MemoryPool<TestObject, POOL_SIZE>();
        objs.fill(nullptr);
        state.ResumeTiming();

        for (size_t i = 0; i < fill_count; ++i) {
            objs[i] = pool.allocate(i, static_cast<int32_t>(i));
        }
        for (size_t i = fill_count; i < POOL_SIZE; ++i) {
            objs[i] = pool.allocate(i, static_cast<int32_t>(i));
        }

        for (size_t i = 0; i < POOL_SIZE; ++i) {
            if (objs[i]) pool.deallocate(objs[i]);
        }
    }
    state.SetItemsProcessed(state.iterations() * POOL_SIZE);
}

// Register MemoryPool benchmarks
BENCHMARK(BM_MemoryPool_Sequential)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_MemoryPool_Random)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_MemoryPool_NearlyFull)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
