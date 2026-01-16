#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// 测试 addOrder 的性能（包含 PriceLevel 链表遍历）
static void BM_AddOrder(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  // 预填充订单簿
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }

  // 测试添加新订单的性能（PriceLevel 链表遍历）
  uint64_t order_id = 1000000;
  for (auto _ : state) {
    // 添加一个不在边缘的新订单，需要遍历链表
    ob.addOrder(order_id++, 1500, 100, Side::BUY);
    ob.addOrder(order_id++, 1500, 100, Side::SELL);
  }

  // 清理
  ob.clear();
}
BENCHMARK(BM_AddOrder)->Range(10, 500);

// 测试 modifyOrder 的性能（可能触发 PriceLevel 查找）
static void BM_ModifyOrder(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");
  std::vector<uint64_t> order_ids;

  // 预填充订单簿
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
    order_ids.push_back(i);
    order_ids.push_back(i + levels);
  }

  // 测试修改订单的性能
  uint64_t idx = 0;
  for (auto _ : state) {
    for (int i = 0; i < 10; i++) {  // 每次迭代修改10个订单
      ob.modifyOrder(order_ids[idx++], 1000 + (i % levels), 200, Side::BUY);
    }
  }
}
BENCHMARK(BM_ModifyOrder)->Range(10, 500);

// 测试 addOrder 批量插入（大量 PriceLevel 链表遍历）
static void BM_AddOrderBatch(benchmark::State& state) {
  const int batch_size = static_cast<int>(state.range(0));
  OrderBook ob("TEST");

  for (auto _ : state) {
    ob.clear();
    // 批量添加订单，每个新订单都需要遍历链表
    for (int i = 0; i < batch_size; i++) {
      // 插入到中间位置，最大化遍历距离
      ob.addOrder(i, 1500 + (i % 100), 100, Side::BUY);
      ob.addOrder(i + batch_size, 1500 + (i % 100), 100, Side::SELL);
    }
  }
}
BENCHMARK(BM_AddOrderBatch)->Range(10, 200);

// 测试 getImbalance 遍历（PriceLevel 链表遍历）
static void BM_GetImbalance(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  const int k = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  // 预填充订单簿
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

// 测试 getBookPressure 遍历
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
