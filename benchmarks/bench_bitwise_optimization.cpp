#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>

// Test modulo vs bitwise AND for hash map indexing
constexpr size_t MAX_ORDERS = 65536;
constexpr size_t MAX_PRICE_LEVELS = 2048;

// Modulo version (original)
static inline size_t hash_modulo(uint64_t key, size_t max_size) {
    return key % max_size;
}

// Bitwise AND version (optimized)
static inline size_t hash_bitwise(uint64_t key, size_t max_size) {
    return static_cast<size_t>(key) & (max_size - 1);
}

// Benchmark modulo operations
static void BM_HashMap_Modulo(benchmark::State& state) {
    size_t mask = MAX_ORDERS - 1;
    volatile size_t result = 0;  // Prevent optimization

    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            result = hash_modulo(i, MAX_ORDERS);
        }
    }
    benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_HashMap_Modulo)->Range(1000, 100000);

// Benchmark bitwise operations
static void BM_HashMap_Bitwise(benchmark::State& state) {
    size_t mask = MAX_ORDERS - 1;
    volatile size_t result = 0;

    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            result = hash_bitwise(i, MAX_ORDERS);
        }
    }
    benchmark::DoNotOptimize(result);
}
BENCHMARK(BM_HashMap_Bitwise)->Range(1000, 100000);

// Benchmark modulo for price levels
static void BM_PriceIndex_Modulo(benchmark::State& state) {
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            int32_t price = static_cast<int32_t>(1000 + (i % 1000));
            size_t idx = static_cast<uint32_t>(price) % MAX_PRICE_LEVELS;
            benchmark::DoNotOptimize(idx);
        }
    }
}
BENCHMARK(BM_PriceIndex_Modulo)->Range(1000, 100000);

// Benchmark bitwise for price levels
static void BM_PriceIndex_Bitwise(benchmark::State& state) {
    size_t mask = MAX_PRICE_LEVELS - 1;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            int32_t price = static_cast<int32_t>(1000 + (i % 1000));
            size_t idx = static_cast<uint32_t>(price) & mask;
            benchmark::DoNotOptimize(idx);
        }
    }
}
BENCHMARK(BM_PriceIndex_Bitwise)->Range(1000, 100000);

// Ring buffer index modulo
static void BM_RingBuffer_Modulo(benchmark::State& state) {
    size_t head = 0;
    size_t count = 50000;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            size_t idx = (head + (MAX_ORDERS - count + i)) % MAX_ORDERS;
            benchmark::DoNotOptimize(idx);
        }
    }
}
BENCHMARK(BM_RingBuffer_Modulo)->Range(1000, 100000);

// Ring buffer index bitwise
static void BM_RingBuffer_Bitwise(benchmark::State& state) {
    size_t head = 0;
    size_t count = 50000;
    size_t mask = MAX_ORDERS - 1;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            size_t idx = (head + (MAX_ORDERS - count + i)) & mask;
            benchmark::DoNotOptimize(idx);
        }
    }
}
BENCHMARK(BM_RingBuffer_Bitwise)->Range(1000, 100000);

// Ring buffer head increment
static void BM_RingBuffer_Increment(benchmark::State& state) {
    size_t head = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            head = (head + 1) % MAX_ORDERS;
            benchmark::DoNotOptimize(head);
        }
    }
}
BENCHMARK(BM_RingBuffer_Increment)->Range(1000, 100000);

// Ring buffer head increment with bitwise
static void BM_RingBuffer_Increment_Bitwise(benchmark::State& state) {
    size_t head = 0;
    size_t mask = MAX_ORDERS - 1;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            head = (head + 1) & mask;
            benchmark::DoNotOptimize(head);
        }
    }
}
BENCHMARK(BM_RingBuffer_Increment_Bitwise)->Range(1000, 100000);

BENCHMARK_MAIN();
