#include <benchmark/benchmark.h>
#include <cstdint>
#include <vector>
#include <random>

#include "impl/order_book.hpp"

using namespace impl;

// ============================================================================
// Hash Collision Benchmark - Test linear probing performance
// ============================================================================

// Benchmark: addOrder with sequential IDs (no collisions expected)
static void BM_AddOrder_Sequential(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    state.ResumeTiming();

    for (size_t i = 0; i < count; ++i) {
      ob.addOrder(i, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_AddOrder_Sequential)->Range(100, 10000);

// Benchmark: addOrder with colliding IDs (1, 65537, 131073, ... all collide)
static void BM_AddOrder_Colliding(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    state.ResumeTiming();

    for (size_t i = 0; i < count; ++i) {
      // These all hash to index 1: 1, 65537, 131073, 196609, ...
      uint64_t order_id = 1 + static_cast<uint64_t>(i) * 65536;
      ob.addOrder(order_id, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_AddOrder_Colliding)->Range(4, 64);  // Smaller range due to potential table full

// Benchmark: addOrder with random IDs (natural collision rate ~N/65536)
static void BM_AddOrder_Random(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));
  std::vector<uint64_t> order_ids(count);

  // Generate random order IDs once
  std::mt19937_64 rng(42);
  for (size_t i = 0; i < count; ++i) {
    order_ids[i] = rng() % 1000000000ULL;  // Large random IDs
  }

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    state.ResumeTiming();

    for (size_t i = 0; i < count; ++i) {
      ob.addOrder(order_ids[i], 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_AddOrder_Random)->Range(100, 10000);

// Benchmark: modifyOrder with sequential IDs
static void BM_ModifyOrder_Sequential(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    // Add orders first
    for (size_t i = 0; i < count; ++i) {
      ob.addOrder(i, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }
    state.ResumeTiming();

    // Now modify them
    for (size_t i = 0; i < count; ++i) {
      ob.modifyOrder(i, 1000 + static_cast<int32_t>(i % 100), 200, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_ModifyOrder_Sequential)->Range(100, 10000);

// Benchmark: modifyOrder with colliding IDs
static void BM_ModifyOrder_Colliding(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    // Add orders first (all colliding)
    for (size_t i = 0; i < count; ++i) {
      uint64_t order_id = 1 + static_cast<uint64_t>(i) * 65536;
      ob.addOrder(order_id, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }
    state.ResumeTiming();

    // Now modify them
    for (size_t i = 0; i < count; ++i) {
      uint64_t order_id = 1 + static_cast<uint64_t>(i) * 65536;
      ob.modifyOrder(order_id, 1000 + static_cast<int32_t>(i % 100), 200, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_ModifyOrder_Colliding)->Range(4, 32);

// Benchmark: deleteOrder with sequential IDs
static void BM_DeleteOrder_Sequential(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    // Add orders first
    for (size_t i = 0; i < count; ++i) {
      ob.addOrder(i, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }
    state.ResumeTiming();

    // Now delete them
    for (size_t i = 0; i < count; ++i) {
      ob.deleteOrder(i, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_DeleteOrder_Sequential)->Range(100, 10000);

// Benchmark: deleteOrder with colliding IDs
static void BM_DeleteOrder_Colliding(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    // Add orders first (all colliding)
    for (size_t i = 0; i < count; ++i) {
      uint64_t order_id = 1 + static_cast<uint64_t>(i) * 65536;
      ob.addOrder(order_id, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }
    state.ResumeTiming();

    // Now delete them
    for (size_t i = 0; i < count; ++i) {
      uint64_t order_id = 1 + static_cast<uint64_t>(i) * 65536;
      ob.deleteOrder(order_id, Side::BUY);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_DeleteOrder_Colliding)->Range(4, 32);

// Benchmark: getOrderRank with sequential IDs
static void BM_GetOrderRank_Sequential(benchmark::State& state) {
  const size_t count = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    OrderBook ob("TEST");
    // Add orders first
    for (size_t i = 0; i < count; ++i) {
      ob.addOrder(i, 1000 + static_cast<int32_t>(i % 100), 100, Side::BUY);
    }
    state.ResumeTiming();

    // Query ranks
    for (size_t i = 0; i < count; ++i) {
      volatile size_t rank = ob.getOrderRank(i);
      benchmark::DoNotOptimize(rank);
    }

    state.PauseTiming();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_GetOrderRank_Sequential)->Range(100, 10000);

//BENCHMARK_MAIN();  // main provided by benchmark_main library
