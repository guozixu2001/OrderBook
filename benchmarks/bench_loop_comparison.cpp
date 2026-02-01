#include <benchmark/benchmark.h>
#include <cstdint>

#include "impl/order_book.hpp"
#include "framework/define.hpp"

using namespace impl;

// Simulate the old getBidLevels (while + break).
static size_t getBidLevels_old(const PriceLevel* bids) {
  size_t count = 0;
  const PriceLevel* current = bids;
  while (current) {
    count++;
    if (current->next == bids) break;
    current = current->next;
  }
  return count;
}

// Simulate the optimized getBidLevels (do-while).
static size_t getBidLevels_new(const PriceLevel* bids) {
  if (!bids) return 0;
  size_t count = 0;
  const PriceLevel* start = bids;
  const PriceLevel* current = start;
  do {
    count++;
    current = current->next;
  } while (current != start);
  return count;
}

// Helper to build test data.
void setup_orderbook(OrderBook& ob, int levels) {
  for (int i = 0; i < levels; i++) {
    ob.addOrder(i, 1000 + i, 100, Side::BUY);
    ob.addOrder(i + levels, 2000 - i, 100, Side::SELL);
  }
}

// Benchmark: old vs optimized (microbenchmark).
static void BM_OldLoop(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");
  setup_orderbook(ob, levels);
  const PriceLevel* bids = ob.getBBO() ? nullptr : nullptr;  // Indirectly fetch bids.

  // Re-fetch the bids pointer (indirectly via getBidPrice).
  int32_t dummy = 0;
  for (int i = 0; i < levels; i++) {
    dummy = ob.getBidPrice(i);
  }

  for (auto _ : state) {
    // Use the optimized version (current implementation).
    volatile size_t count = ob.getBidLevels();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_OldLoop)->Range(10, 500);

// Comparison test: manual old implementation.
static void BM_OldLoopManual(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");
  setup_orderbook(ob, levels);

  // Trick to get bids pointer: access via getBidPrice.
  int32_t first_bid = ob.getBidPrice(0);
  benchmark::DoNotOptimize(first_bid);

  for (auto _ : state) {
    // Simulate the old version.
    size_t count = 0;
    int32_t price = 0;
    do {
      price = ob.getBidPrice(count);
      count++;
    } while (count < static_cast<size_t>(levels) && price != 0);
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_OldLoopManual)->Range(10, 500);

// More precise microbenchmark: inline comparison.
struct ListNode {
  int value;
  ListNode* next;
  ListNode* self_ref;
};

static ListNode* create_list(int size) {
  if (size == 0) return nullptr;
  auto* nodes = new ListNode[size];
  for (int i = 0; i < size; i++) {
    nodes[i].value = i;
    nodes[i].next = &nodes[i + 1 < size ? i + 1 : 0];
    nodes[i].self_ref = &nodes[i];
  }
  return nodes;
}

// Old version (while + break).
static void BM_OldLoopInline(benchmark::State& state) {
  const int size = static_cast<int>(state.range(0));
  ListNode* head = create_list(size);

  for (auto _ : state) {
    volatile int sum = 0;
    ListNode* current = head;
    while (current) {
      sum += current->value;
      if (current->next == head) break;
      current = current->next;
    }
    benchmark::DoNotOptimize(sum);
  }

  delete[] head;
}
BENCHMARK(BM_OldLoopInline)->Range(10, 1000);

// Optimized version (do-while).
static void BM_NewLoopInline(benchmark::State& state) {
  const int size = static_cast<int>(state.range(0));
  ListNode* head = create_list(size);

  for (auto _ : state) {
    volatile int sum = 0;
    if (head) {
      ListNode* start = head;
      ListNode* current = start;
      do {
        sum += current->value;
        current = current->next;
      } while (current != start);
    }
    benchmark::DoNotOptimize(sum);
  }

  delete[] head;
}
BENCHMARK(BM_NewLoopInline)->Range(10, 1000);

// Real OrderBook metrics test.
static void BM_GetLevelsOptimized(benchmark::State& state) {
  const int levels = static_cast<int>(state.range(0));
  OrderBook ob("TEST");
  setup_orderbook(ob, levels);

  for (auto _ : state) {
    volatile size_t bid_levels = ob.getBidLevels();
    volatile size_t ask_levels = ob.getAskLevels();
    benchmark::DoNotOptimize(bid_levels + ask_levels);
  }
}
BENCHMARK(BM_GetLevelsOptimized)->Range(10, 500);

//BENCHMARK_MAIN(); // main provided by benchmark_main library
