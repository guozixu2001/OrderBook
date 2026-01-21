#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdio>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// 原始版本 (while + break) - 用于对比
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

// 微基准: 模拟原始 while + break 模式
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

// 微基准: 优化后的 do-while 模式
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

// 实际 OrderBook 测试: getBidLevels + getAskLevels
static void BM_OrderBookGetLevels(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  // 添加测试数据
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

// 测试 getImbalance (遍历 k 个档位)
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

// 测试 getBookPressure (遍历 k 个档位)
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
