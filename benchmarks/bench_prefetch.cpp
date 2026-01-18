#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// Measure addOrder performance (includes PriceLevel list traversal).
static void BM_AddOrder(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  // Pre-fill the order book.
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  // Measure adding new orders (PriceLevel list traversal).
  uint64_t order_id = 1000000;
  for (auto _ : state) {
    // Add a non-edge order to force list traversal.
    ob.addOrder(order_id++, 1500, 100, Side::BUY);
    ob.addOrder(order_id++, 1500, 100, Side::SELL);
  }

  // Cleanup.
  ob.clear();
}
BENCHMARK(BM_AddOrder)->Range(10, 500);

// Measure modifyOrder performance (may trigger PriceLevel lookup).
static void BM_ModifyOrder(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");
  std::vector<uint64_t> order_ids;

  // Pre-fill the order book.
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
    order_ids.push_back(i);
    order_ids.push_back(i + levels);
  }

  // Measure modifying orders.
  uint64_t idx = 0;
  for (auto _ : state) {
    for (int i = 0; i < 10; i++) {  // Modify 10 orders per iteration.
      ob.modifyOrder(order_ids[idx++], 1000 + (i % levels), 200, Side::BUY);
    }
  }
}
BENCHMARK(BM_ModifyOrder)->Range(10, 500);

// Measure batched addOrder (heavy PriceLevel list traversal).
static void BM_AddOrderBatch(benchmark::State& state) {
  const int batch_size = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  for (auto _ : state) {
    ob.clear();
    // Batch insert; each new order traverses the list.
    for (int i = 0; i < batch_size; i++) {
      // Insert into the middle to maximize traversal distance.
      ob.addOrder(i, 1500 + (i % 100), 100, Side::BUY);
      ob.addOrder(i + batch_size, 1500 + (i % 100), 100, Side::SELL);
    }
  }
}
BENCHMARK(BM_AddOrderBatch)->Range(10, 200);

// Measure getImbalance traversal (PriceLevel list traversal).
static void BM_GetImbalance(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  const int k = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  // Pre-fill the order book.
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  for (auto _ : state) {
    volatile double imbalance = ob.getImbalance(k);
    benchmark::DoNotOptimize(imbalance);
  }
}
BENCHMARK(BM_GetImbalance)->Ranges({{10, 500}, {5, 100}});

// Measure getBookPressure traversal.
static void BM_GetBookPressure(benchmark::State& state) {
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
BENCHMARK(BM_GetBookPressure)->Ranges({{10, 500}, {5, 100}});

//BENCHMARK_MAIN(); // main provided by benchmark_main library
