#include <benchmark/benchmark.h>
#include <cstdint>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// Benchmark: findPriceLevel - existing price lookup.
static void BM_FindPriceLevelHit(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    for (int i = 0; i < levels; i++) {
      volatile PriceLevel* level = ob.findPriceLevel(1000 + i);
      benchmark::DoNotOptimize(level);
    }
  }
}
BENCHMARK(BM_FindPriceLevelHit)->Range(10, 500);

// Benchmark: findPriceLevel - missing price lookup.
static void BM_FindPriceLevelMiss(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    for (int i = 0; i < levels; i++) {
      volatile PriceLevel* level = ob.findPriceLevel(3000 + i);  // Missing price.
      benchmark::DoNotOptimize(level);
    }
  }
}
BENCHMARK(BM_FindPriceLevelMiss)->Range(10, 500);

// Benchmark: findPriceLevel - mixed lookup.
static void BM_FindPriceLevelMixed(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    for (int i = 0; i < levels; i++) {
      // 80% hit, 20% miss
      int32_t price = (i % 5 < 4) ? (1000 + i) : (3000 + i);
      volatile PriceLevel* level = ob.findPriceLevel(price);
      benchmark::DoNotOptimize(level);
    }
  }
}
BENCHMARK(BM_FindPriceLevelMixed)->Range(10, 500);

//BENCHMARK_MAIN();  // main provided by benchmark_main library
