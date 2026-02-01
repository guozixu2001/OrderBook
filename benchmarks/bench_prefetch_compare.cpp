#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// Microbenchmark for list traversal (compare with/without prefetch).
struct Node {
  int value;
  Node* next;
};

static Node* create_list(int size) {
  if (size == 0) return nullptr;
  auto* nodes = new Node[size];
  for (int i = 0; i < size; i++) {
    nodes[i].value = i;
    nodes[i].next = &nodes[i + 1 < size ? i + 1 : 0];  // Circular list.
  }
  return nodes;
}

// Version 1: no prefetch, baseline pattern.
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

// Version 2: with prefetch, optimized pattern.
static void BM_TraverseWithPrefetch(benchmark::State& state) {
  const int size = static_cast<int>(state.range(0));
  Node* head = create_list(size);

  for (auto _ : state) {
    volatile int sum = 0;
    Node* current = head;
    while (current->next != head && current->next->value < 500000) {
      sum += current->value;
      // Prefetch the next-next node's next pointer.
      __builtin_prefetch(current->next->next, 0, 3);
      current = current->next;
    }
    benchmark::DoNotOptimize(sum);
  }

  delete[] head;
}
BENCHMARK(BM_TraverseWithPrefetch)->Range(10, 2000);

// OrderBook test: batched addOrder (mid-list inserts to force traversal).
static void BM_AddOrderMiddle(benchmark::State& state) {
  const int initial_levels = static_cast<int>(state.range(0));
  const int batch_size = static_cast<int>(state.range(1));
  OrderBook ob("TEST");

  // Pre-fill the order book to build ordered lists.
  for (int i = 0; i < initial_levels; i++) {
    ob.addOrder(i, 1000 + i * 10, 100, Side::BUY);
    ob.addOrder(i + initial_levels, 2000 - i * 10, 100, Side::SELL);
  }

  for (auto _ : state) {
    ob.clear();
    // Rebuild the order book.
    for (int i = 0; i < initial_levels; i++) {
      ob.addOrder(i, 1000 + i * 10, 100, Side::BUY);
      ob.addOrder(i + initial_levels, 2000 - i * 10, 100, Side::SELL);
    }
    // Batch insert into the middle of the list (forces traversal).
    for (int i = 0; i < batch_size; i++) {
      int price = 1400 + (i % 100);  // Insert into the middle.
      ob.addOrder(1000000 + i, price, 100, Side::BUY);
      ob.addOrder(2000000 + i, price, 100, Side::SELL);
    }
  }
}
BENCHMARK(BM_AddOrderMiddle)->Ranges({{10, 200}, {10, 100}});

//BENCHMARK_MAIN(); // main provided by benchmark_main library
