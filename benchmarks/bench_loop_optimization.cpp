#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdio>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// Old version (while + break) for comparison.
// size_t getBidLevels_old(const PriceLevel* bids) {
//   size_t count = 0;
//   const PriceLevel* current = bids;
//   while (current) {
//     count++;
//     if (current->next == bids) break;
//     current = current->next;
//   }
//   return count;
// }

// Microbenchmark: simulate the old while + break pattern.
static void BM_OldLoopPattern(benchmark::State& state) {
  const int head = static_cast<int>(state.range(0));
  volatile int sum = 0;

  for (auto _ : state) {
    int count = 0;
    int current = 0;
    while (current < head) {
      count++;
      if (current + 1 == head) break;
      current++;
    }
    sum = count;
  }
}
BENCHMARK(BM_OldLoopPattern)->Range(10, 1000);

// Microbenchmark: optimized do-while pattern.
static void BM_NewLoopPattern(benchmark::State& state) {
  const int head = static_cast<int>(state.range(0));
  volatile int sum = 0;

  for (auto _ : state) {
    int count = 0;
    int current = 0;
    do {
      count++;
      current++;
    } while (current < head);
    sum = count;
  }
}
BENCHMARK(BM_NewLoopPattern)->Range(10, 1000);

// OrderBook test: getBidLevels + getAskLevels.
static void BM_OrderBookGetLevels(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  // Add test data.
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    volatile size_t bid_levels = ob.getBidLevels();
    volatile size_t ask_levels = ob.getAskLevels();
    benchmark::DoNotOptimize(bid_levels + ask_levels);
  }
}
BENCHMARK(BM_OrderBookGetLevels)->Range(10, 500);

// Test getImbalance (traverse k levels).
static void BM_OrderBookGetImbalance(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  const int k = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    volatile double imbalance = ob.getImbalance(k);
    benchmark::DoNotOptimize(imbalance);
  }
}
BENCHMARK(BM_OrderBookGetImbalance)->Ranges({{10, 500}, {5, 100}});

// Test getBookPressure (traverse k levels).
static void BM_OrderBookGetBookPressure(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  const int k = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    volatile double pressure = ob.getBookPressure(k);
    benchmark::DoNotOptimize(pressure);
  }
}
BENCHMARK(BM_OrderBookGetBookPressure)->Ranges({{10, 500}, {5, 100}});

//BENCHMARK_MAIN(); // main provided by benchmark_main library
