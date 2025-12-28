#include <benchmark/benchmark.h>
#include "impl/order_book.hpp"
#include "framework/define.hpp"
#include <memory>

using namespace impl;

// ==========================================
// Benchmark: Add Order
// ==========================================
static void BM_AddOrder(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming(); 
        auto ob_ptr = std::make_unique<OrderBook>("TEST");
        state.ResumeTiming(); 

        for (int i = 0; i < state.range(0); ++i) {
            ob_ptr->addOrder(i, 100 + (i % 10), 10, Side::BUY);
        }

        state.PauseTiming(); 
        ob_ptr.reset(); 
        state.ResumeTiming(); 
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
// Range(1000, 10000) 表示每次 batch 插入 1000 到 10000 个订单
BENCHMARK(BM_AddOrder)->Range(1000, 10000); 

// ==========================================
// Benchmark: Imbalance 
// ==========================================
static void BM_GetImbalance(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    for (int i = 0; i < 1000; ++i) {
        ob_ptr->addOrder(i * 2, 100, 10, Side::BUY);     
        ob_ptr->addOrder(i * 2 + 1, 102, 10, Side::SELL); 
    }

    for (auto _ : state) {
        double result = ob_ptr->getImbalance(10);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_GetImbalance);

// ==========================================
// Benchmark: Process Trade
// ==========================================
static void BM_ProcessTrade(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    ob_ptr->addOrder(99999, 100, 2000000000, Side::BUY);
    
    uint64_t t = 1000000000ULL;

    for (auto _ : state) {
        ob_ptr->processTrade(99999, 1, 100, 10, Side::BUY, t);
        t += 1000; 
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ProcessTrade);

// ==========================================
// Benchmark: VWAP 
// ==========================================
static void BM_GetVWAP(benchmark::State& state) {
    auto ob_ptr = std::make_unique<OrderBook>("TEST");
    uint64_t t = 1000000000ULL;
    ob_ptr->addOrder(1, 100, 1000000, Side::BUY);
    
    for(int i=0; i<2000; ++i) {
        ob_ptr->processTrade(1, i, 100, 10, Side::BUY, t + i);
    }

    for (auto _ : state) {
        uint64_t vwap = ob_ptr->getVWAP();
        benchmark::DoNotOptimize(vwap);
    }
}
BENCHMARK(BM_GetVWAP);

BENCHMARK_MAIN();
