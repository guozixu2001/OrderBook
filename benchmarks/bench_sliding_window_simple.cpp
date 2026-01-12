#include <benchmark/benchmark.h>
#include "impl/sliding_window.hpp"
#include "impl/sliding_window_ring.hpp"
#include <iostream>

using namespace impl;

// ============================================================================
// Simple correctness and performance test
// ============================================================================

// Test recordTrade - create new object for each iteration
static void BM_Original_recordTrade(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats* stats = new SlidingWindowStats();
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            uint64_t ts = 1700000000ULL + i;
            int32_t price = 100 + (i % 100);
            uint64_t qty = 10;
            stats->recordTrade(ts, price, qty);
        }

        state.PauseTiming();
        delete stats;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Original_recordTrade)->Range(100, 1000);

static void BM_RingBuffer_recordTrade(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats* stats = new RingBufferSlidingWindowStats();
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            uint64_t ts = 1700000000ULL + i;
            int32_t price = 100 + (i % 100);
            uint64_t qty = 10;
            stats->recordTrade(ts, price, qty);
        }

        state.PauseTiming();
        delete stats;
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_RingBuffer_recordTrade)->Range(100, 1000);

// Test getPriceRange
static void BM_Original_getPriceRange(benchmark::State& state) {
    SlidingWindowStats stats;

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

// Test evictExpired
static void BM_Original_evictExpired(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        SlidingWindowStats* stats = new SlidingWindowStats();

        // Populate with trades spread over 15 minutes
        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 90);
            stats->recordTrade(ts, 100 + (i % 1000), 100);
        }
        state.ResumeTiming();

        stats->evictExpired(20240101151500ULL);

        state.PauseTiming();
        delete stats;
        state.ResumeTiming();
    }
}
BENCHMARK(BM_Original_evictExpired);

static void BM_RingBuffer_evictExpired(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        RingBufferSlidingWindowStats* stats = new RingBufferSlidingWindowStats();

        for (int i = 0; i < 10000; ++i) {
            uint64_t ts = 1700000000ULL + (i * 90);
            stats->recordTrade(ts, 100 + (i % 1000), 100);
        }
        state.ResumeTiming();

        stats->evictExpired(20240101151500ULL);

        state.PauseTiming();
        delete stats;
        state.ResumeTiming();
    }
}
BENCHMARK(BM_RingBuffer_evictExpired);

// Test VWAP (should be identical)
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

// Correctness test
static void Correctness_test(benchmark::State& state) {
    SlidingWindowStats original;
    RingBufferSlidingWindowStats ringbuffer;

    // Add 100 trades
    for (int i = 0; i < 100; ++i) {
        uint64_t ts = 1700000000ULL + i;
        int32_t price = 100 + (i % 50);  // 100-149
        original.recordTrade(ts, price, 100);
        ringbuffer.recordTrade(ts, price, 100);
    }

    int32_t orig_range = original.getPriceRange();
    int32_t ring_range = ringbuffer.getPriceRange();

    // Check VWAP
    uint64_t orig_vwap = original.getVWAP();
    uint64_t ring_vwap = ringbuffer.getVWAP();

    std::cout << "=== Correctness Test Results ===" << std::endl;
    std::cout << "Original getPriceRange: " << orig_range << std::endl;
    std::cout << "RingBuffer getPriceRange: " << ring_range << std::endl;
    std::cout << "Original getVWAP: " << orig_vwap << std::endl;
    std::cout << "RingBuffer getVWAP: " << ring_vwap << std::endl;
    std::cout << "Ranges match: " << (orig_range == ring_range ? "YES" : "NO") << std::endl;
    std::cout << "VWAPs match: " << (orig_vwap == ring_vwap ? "YES" : "NO") << std::endl;

    // Dummy benchmark to make this run
    for (auto _ : state) {
        benchmark::DoNotOptimize(orig_range);
    }
}
BENCHMARK(Correctness_test);

BENCHMARK_MAIN();
