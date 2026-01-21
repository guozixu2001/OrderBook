#include <benchmark/benchmark.h>
#include <vector>
#include <algorithm>
#include <numeric>

#include "impl/sliding_window_ring.hpp"

using namespace impl;

// ============================================================================
// getMedianPrice Benchmark
// Tests the O(N) -> O(1) optimization for median calculation
// ============================================================================

// Helper: O(N) median calculation (simulates old implementation)
static int32_t naiveMedian(const std::vector<int32_t>& prices) {
  if (prices.empty()) return 0;
  std::vector<int32_t> copy = prices;
  size_t n = copy.size();
  size_t mid = n / 2;
  std::nth_element(copy.begin(), copy.begin() + mid, copy.end());
  if (n % 2 == 0) {
    int32_t mid1 = copy[mid];
    std::nth_element(copy.begin(), copy.begin() + mid - 1, copy.end());
    return (copy[mid - 1] + mid1) / 2;
  }
  return copy[mid];
}

// Benchmark: getMedianPrice with RingBuffer (optimized O(1))
static void BM_RingBuffer_getMedianPrice(benchmark::State& state) {
  const size_t num_trades = static_cast<size_t>(state.range(0));
  RingBufferSlidingWindowStats stats;

  // Populate with trades
  for (size_t i = 0; i < num_trades; ++i) {
    uint64_t ts = 1700000000ULL + i;
    int32_t price = 100 + static_cast<int32_t>(i % 1000);  // 100-1099
    stats.recordTrade(ts, price, 100);
  }

  for (auto _ : state) {
    int32_t median = stats.getMedianPrice();
    benchmark::DoNotOptimize(median);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RingBuffer_getMedianPrice)->Range(100, 65536);

// Benchmark: Naive O(N) median calculation (simulates old implementation)
static void BM_Naive_getMedianPrice(benchmark::State& state) {
  const size_t num_trades = static_cast<size_t>(state.range(0));
  std::vector<int32_t> prices;

  // Populate with trades (simulate same pattern)
  for (size_t i = 0; i < num_trades; ++i) {
    int32_t price = 100 + static_cast<int32_t>(i % 1000);
    prices.push_back(price);
  }

  for (auto _ : state) {
    int32_t median = naiveMedian(prices);
    benchmark::DoNotOptimize(median);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Naive_getMedianPrice)->Range(100, 65536);

// Benchmark: getMedianPrice after eviction
static void BM_RingBuffer_getMedianPrice_AfterEvict(benchmark::State& state) {
  const size_t num_trades = static_cast<size_t>(state.range(0));
  RingBufferSlidingWindowStats stats;

  // Populate with trades spread over 15 minutes (expire 5 minutes)
  for (size_t i = 0; i < num_trades; ++i) {
    uint64_t ts = 1700000000ULL + (i * 90);  // 90 seconds apart
    int32_t price = 100 + static_cast<int32_t>(i % 1000);
    stats.recordTrade(ts, price, 100);
  }

  // Evict trades older than 10 minutes
  stats.evictExpired(20240101151000ULL);

  for (auto _ : state) {
    int32_t median = stats.getMedianPrice();
    benchmark::DoNotOptimize(median);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RingBuffer_getMedianPrice_AfterEvict)->Range(1000, 65536);

// Benchmark: Mixed workload - record + getMedian
static void BM_RingBuffer_RecordThenMedian(benchmark::State& state) {
  const size_t num_trades = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    RingBufferSlidingWindowStats stats;
    state.ResumeTiming();

    for (size_t i = 0; i < num_trades; ++i) {
      uint64_t ts = 1700000000ULL + i;
      int32_t price = 100 + static_cast<int32_t>(i % 1000);
      stats.recordTrade(ts, price, 100);

      // Query median every 100 trades
      if (i % 100 == 99) {
        int32_t median = stats.getMedianPrice();
        benchmark::DoNotOptimize(median);
      }
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * num_trades);
}
BENCHMARK(BM_RingBuffer_RecordThenMedian)->Range(100, 10000);

//BENCHMARK_MAIN();  // main provided by benchmark_main library
