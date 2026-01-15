#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// 模拟链表遍历的微基准测试（对比有无预取）
struct Node {
  int value;
  Node* next;
};

static Node* create_list(int size) {
  if (size == 0) return nullptr;
  auto* nodes = new Node[size];
  for (int i = 0; i < size; i++) {
    nodes[i].value = i;
    nodes[i].next = &nodes[i + 1 < size ? i + 1 : 0];  // 循环链表
  }
  return nodes;
}

// 版本1: 无预取 - 原始代码模式
static void BM_TraverseNoPrefetch(benchmark::State& state) {
  const int size = static_cast<int>(state.range(0));
  Node* head = create_list(size);

  for (auto _ : state) {
    volatile int sum = 0;
    Node* current = head;
    while (current->next != head && current->next->value < 500000) {
      sum += current->value;
      current = current->next;
    }
    benchmark::DoNotOptimize(sum);
  }

  delete[] head;
}
BENCHMARK(BM_TraverseNoPrefetch)->Range(10, 2000);

// 版本2: 有预取 - 优化代码模式
static void BM_TraverseWithPrefetch(benchmark::State& state) {
  const int size = static_cast<int>(state.range(0));
  Node* head = create_list(size);

  for (auto _ : state) {
    volatile int sum = 0;
    Node* current = head;
    while (current->next != head && current->next->value < 500000) {
      sum += current->value;
      // 预取下下个节点的 next 指针
      __builtin_prefetch(current->next->next, 0, 3);
      current = current->next;
    }
    benchmark::DoNotOptimize(sum);
  }

  delete[] head;
}
BENCHMARK(BM_TraverseWithPrefetch)->Range(10, 2000);

// 实际 OrderBook 测试: addOrder 批量插入（中间位置插入，强制遍历）
static void BM_AddOrderMiddle(benchmark::State& state) {
  const int initial_levels = static_cast<int>(state.range(0));
  const int batch_size = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  // 预填充订单簿，创建有序链表
  for (int i = 0; i < initial_levels; i++) {
    ob.addOrder(i, 1000 + i * 10, 100, Side::BUY);
    ob.addOrder(i + initial_levels, 2000 - i * 10, 100, Side::SELL);
  }

  for (auto _ : state) {
    ob.clear();
    // 重建订单簿
    for (int i = 0; i < initial_levels; i++) {
      ob.addOrder(i, 1000 + i * 10, 100, Side::BUY);
      ob.addOrder(i + initial_levels, 2000 - i * 10, 100, Side::SELL);
    }
    // 批量插入到链表中间位置（强制遍历）
    for (int i = 0; i < batch_size; i++) {
      int price = 1400 + (i % 100);  // 插入到中间位置
      ob.addOrder(1000000 + i, price, 100, Side::BUY);
      ob.addOrder(2000000 + i, price, 100, Side::SELL);
    }
  }
}
BENCHMARK(BM_AddOrderMiddle)->Ranges({{10, 200}, {10, 100}});

BENCHMARK_MAIN();
