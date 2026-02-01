#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <vector>

#include "impl/order_book.hpp"

using namespace impl;

namespace {

constexpr uint64_t kMaxOrderId = MAX_ORDERS - 1;
constexpr int32_t kBasePrice = 100000;  // cents
constexpr int32_t kMaxQty = 1000;

struct WorkloadConfig {
  uint32_t ops_per_iter;
  uint32_t trade_percent;
  uint32_t init_orders;
  uint32_t max_active;
  uint32_t price_levels;
};

struct WorkloadState {
  OrderBook ob;
  std::vector<uint64_t> active_ids;
  std::vector<uint32_t> active_pos;
  std::vector<uint64_t> free_ids;
  std::vector<int32_t> order_price;
  std::vector<uint32_t> order_qty;
  std::vector<Side> order_side;

  explicit WorkloadState() : ob("TEST") {
    active_pos.resize(kMaxOrderId + 1, UINT32_MAX);
    order_price.resize(kMaxOrderId + 1, 0);
    order_qty.resize(kMaxOrderId + 1, 0);
    order_side.resize(kMaxOrderId + 1, Side::BUY);
  }

  void reserve(uint32_t max_active) {
    active_ids.reserve(max_active);
    free_ids.reserve(kMaxOrderId);
  }

  void reset(const WorkloadConfig& cfg) {
    ob.clear();
    active_ids.clear();
    std::fill(active_pos.begin(), active_pos.end(), UINT32_MAX);
    free_ids.clear();

    for (uint64_t id = 1; id <= kMaxOrderId; ++id) {
      free_ids.push_back(id);
    }

    for (uint32_t i = 0; i < cfg.init_orders; ++i) {
      addNewOrder(cfg, i);
    }
  }

  uint64_t popFreeId() {
    if (free_ids.empty()) return 0;
    uint64_t id = free_ids.back();
    free_ids.pop_back();
    return id;
  }

  void pushFreeId(uint64_t id) {
    if (id == 0) return;
    free_ids.push_back(id);
  }

  void trackActive(uint64_t id) {
    active_pos[id] = static_cast<uint32_t>(active_ids.size());
    active_ids.push_back(id);
  }

  void untrackActive(uint64_t id) {
    uint32_t pos = active_pos[id];
    if (pos == UINT32_MAX) return;
    uint32_t last_pos = static_cast<uint32_t>(active_ids.size() - 1);
    uint64_t last_id = active_ids[last_pos];
    active_ids[pos] = last_id;
    active_pos[last_id] = pos;
    active_ids.pop_back();
    active_pos[id] = UINT32_MAX;
  }

  void addNewOrder(const WorkloadConfig& cfg, uint32_t seed) {
    if (active_ids.size() >= cfg.max_active) return;
    uint64_t id = popFreeId();
    if (id == 0) return;

    Side side = (seed & 1U) ? Side::BUY : Side::SELL;
    int32_t offset = static_cast<int32_t>((seed % cfg.price_levels) + 1);
    int32_t price = (side == Side::BUY) ? (kBasePrice - offset) : (kBasePrice + offset);
    uint32_t qty = (seed % kMaxQty) + 1;

    ob.addOrder(id, price, qty, side);

    order_price[id] = price;
    order_qty[id] = qty;
    order_side[id] = side;
    trackActive(id);
  }
};

// Simple LCG RNG for repeatability without heavy overhead.
struct LcgRng {
  uint64_t state = 0x9e3779b97f4a7c15ULL;
  uint32_t next() {
    state = state * 2862933555777941757ULL + 3037000493ULL;
    return static_cast<uint32_t>(state >> 33);
  }
};

}  // namespace

static void BM_OrderBookWorkload(benchmark::State& state) {
  const WorkloadConfig cfg{
    static_cast<uint32_t>(state.range(0)),
    static_cast<uint32_t>(state.range(1)),
    20000,  // init_orders
    50000,  // max_active
    2000    // price_levels
  };

  WorkloadState ws;
  ws.reserve(cfg.max_active);

  LcgRng rng;

  for (auto _ : state) {
    state.PauseTiming();
    ws.reset(cfg);
    uint64_t timestamp = 0;
    state.ResumeTiming();

    const uint32_t trade_threshold = cfg.trade_percent;
    const uint32_t add_threshold = trade_threshold + ((100 - trade_threshold) / 2);

    for (uint32_t i = 0; i < cfg.ops_per_iter; ++i) {
      uint32_t r = rng.next() % 100;

      if (r < trade_threshold) {
        if (ws.active_ids.empty()) {
          ws.addNewOrder(cfg, rng.next());
          continue;
        }
        uint32_t pick = rng.next() % ws.active_ids.size();
        uint64_t id = ws.active_ids[pick];
        uint32_t qty = ws.order_qty[id];
        if (qty == 0) continue;
        uint32_t trade_qty = (rng.next() % qty) + 1;
        ws.ob.processTrade(id, 1, ws.order_price[id], trade_qty, ws.order_side[id], timestamp++);
        if (trade_qty >= qty) {
          ws.untrackActive(id);
          ws.pushFreeId(id);
          ws.order_qty[id] = 0;
        } else {
          ws.order_qty[id] -= trade_qty;
        }
        continue;
      }

      if (r < add_threshold) {
        ws.addNewOrder(cfg, rng.next());
        continue;
      }

      if (ws.active_ids.empty()) {
        ws.addNewOrder(cfg, rng.next());
        continue;
      }

      uint32_t pick = rng.next() % ws.active_ids.size();
      uint64_t id = ws.active_ids[pick];
      ws.ob.deleteOrder(id, ws.order_side[id]);
      ws.untrackActive(id);
      ws.pushFreeId(id);
      ws.order_qty[id] = 0;
    }

    benchmark::DoNotOptimize(ws.ob.getBBO());
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * cfg.ops_per_iter);
}

BENCHMARK(BM_OrderBookWorkload)
  ->Args({20000, 10})
  ->Args({50000, 10})
  ->Args({100000, 10})
  ->ArgNames({"ops", "trade_pct"});

