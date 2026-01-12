#include <benchmark/benchmark.h>
#include "impl/sliding_window.hpp"
#include "impl/sliding_window_ring.hpp"
#include <memory>
#include <vector>
#include <random>

using namespace impl;

// ============================================================================
// Comparison Benchmark: Original (Heap-based) vs RingBuffer Implementation
// ============================================================================

// ============================================================================
// Benchmark: recordTrade comparison
// ============================================================================

static void BM_Original_recordTrade(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats stats;
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            uint64_t ts = 1700000000ULL + i;  // Unix timestamp in seconds
            int32_t price = 100 + (i % 100);
            uint64_t qty = 10 + (i % 1000);
            stats.recordTrade(ts, price, qty);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Original_recordTrade)->Range(1000, 10000);

static void BM_RingBuffer_recordTrade(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats stats;
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            uint64_t ts = 1700000000ULL + i;
            int32_t price = 100 + (i % 100);
            uint64_t qty = 10 + (i % 1000);
            stats.recordTrade(ts, price, qty);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_RingBuffer_recordTrade)->Range(1000, 10000);

// ============================================================================
// Benchmark: getPriceRange comparison (after many records)
// ============================================================================

static void BM_Original_getPriceRange(benchmark::State& state) {
    SlidingWindowStats stats;

    // Pre-populate with 5000 trades
    for (int i = 0; i < 5000; ++i) {
        uint64_t ts = 1700000000ULL + i;
        int32_t price = 100 + (i % 1000);  // Prices 100-1099
        stats.recordTrade(ts, price, 100);
    }

    for (auto _ : state) {
        int32_t range = stats.getPriceRange();
        benchmark::DoNotOptimize(range);
    }
}
BENCHMARK(BM_Original_getPriceRange);

static void BM_RingBuffer_getPriceRange(benchmark::State& state) {
    RingBufferSlidingWindowStats stats;

    for (int i = 0; i < 5000; ++i) {
        uint64_t ts = 1700000000ULL + i;
        int32_t price = 100 + (i % 1000);
        stats.recordTrade(ts, price, 100);
    }

    for (auto _ : state) {
        int32_t range = stats.getPriceRange();
        benchmark::DoNotOptimize(range);
    }
}
BENCHMARK(BM_RingBuffer_getPriceRange);

// ============================================================================
// Benchmark: evictExpired comparison
// ============================================================================

static void BM_Original_evictExpired(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats stats;

        // Pre-populate with 10000 trades over 15 minutes
        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 90);  // 90 seconds apart
            stats.recordTrade(ts, 100 + (i % 1000), 100);
        }
        state.ResumeTiming();

        // Evict trades older than 10 minutes
        // Using a valid date format
        stats.evictExpired(20240101151500ULL);
    }
}
BENCHMARK(BM_Original_evictExpired);

static void BM_RingBuffer_evictExpired(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats stats;

        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 90);
            stats.recordTrade(ts, 100 + (i % 1000), 100);
        }
        state.ResumeTiming();

        stats.evictExpired(20240101151500ULL);
    }
}
BENCHMARK(BM_RingBuffer_evictExpired);

// ============================================================================
// Benchmark: Mixed workload (recordTrade + getPriceRange + evictExpired)
// ============================================================================

static void BM_Original_mixedWorkload(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats stats;
        uint64_t current_time = 20240101151500ULL;
        state.ResumeTiming();

        // Simulate trading for 5 minutes (300 seconds)
        for (int i = 0; i < 5000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 60);  // 1 min apart
            stats.recordTrade(ts, 100 + (i % 500), 100);

            // Every 100 trades, query price range
            if (i % 100 == 0) {
                int32_t range = stats.getPriceRange();
                benchmark::DoNotOptimize(range);
            }
        }

        // Evict trades
        stats.evictExpired(current_time);
    }
}
BENCHMARK(BM_Original_mixedWorkload);

static void BM_RingBuffer_mixedWorkload(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats stats;
        uint64_t current_time = 20240101151500ULL;
        state.ResumeTiming();

        for (int i = 0; i < 5000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 60);
            stats.recordTrade(ts, 100 + (i % 500), 100);

            if (i % 100 == 0) {
                int32_t range = stats.getPriceRange();
                benchmark::DoNotOptimize(range);
            }
        }

        stats.evictExpired(current_time);
    }
}
BENCHMARK(BM_RingBuffer_mixedWorkload);

// ============================================================================
// Benchmark: VWAP (should be identical performance)
// ============================================================================

static void BM_Original_getVWAP(benchmark::State& state) {
    SlidingWindowStats stats;

    for (int i = 0; i < 5000; ++i) {
        uint64_t ts = 1700000000ULL + i;
        stats.recordTrade(ts, 100 + (i % 100), 100);
    }

    for (auto _ : state) {
        uint64_t vwap = stats.getVWAP();
        benchmark::DoNotOptimize(vwap);
    }
}
BENCHMARK(BM_Original_getVWAP);

static void BM_RingBuffer_getVWAP(benchmark::State& state) {
    RingBufferSlidingWindowStats stats;

    for (int i = 0; i < 5000; ++i) {
        uint64_t ts = 1700000000ULL + i;
        stats.recordTrade(ts, 100 + (i % 100), 100);
    }

    for (auto _ : state) {
        uint64_t vwap = stats.getVWAP();
        benchmark::DoNotOptimize(vwap);
    }
}
BENCHMARK(BM_RingBuffer_getVWAP);

// ============================================================================
// Benchmark: High frequency trading scenario
// ============================================================================

static void BM_Original_highFrequency(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats stats;
        state.ResumeTiming();

        std::mt19937_64 rng(42);
        std::uniform_int_distribution<int32_t> price_dist(100, 200);
        std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

        // Simulate high-frequency trading: 10000 trades
        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + i;  // 1 second apart
            stats.recordTrade(ts, price_dist(rng), qty_dist(rng));
        }

        // Query price range
        int32_t range = stats.getPriceRange();
        benchmark::DoNotOptimize(range);
    }
}
BENCHMARK(BM_Original_highFrequency);

static void BM_RingBuffer_highFrequency(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats stats;
        state.ResumeTiming();

        std::mt19937_64 rng(42);
        std::uniform_int_distribution<int32_t> price_dist(100, 200);
        std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + i;
            stats.recordTrade(ts, price_dist(rng), qty_dist(rng));
        }

        int32_t range = stats.getPriceRange();
        benchmark::DoNotOptimize(range);
    }
}
BENCHMARK(BM_RingBuffer_highFrequency);

// ============================================================================
// Benchmark: Initialization (memory footprint)
// ============================================================================

static void BM_Original_init(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats stats;
        state.ResumeTiming();
        benchmark::DoNotOptimize(stats);
    }
}
BENCHMARK(BM_Original_init);

static void BM_RingBuffer_init(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats stats;
        state.ResumeTiming();
        benchmark::DoNotOptimize(stats);
    }
}
BENCHMARK(BM_RingBuffer_init);

BENCHMARK_MAIN();
