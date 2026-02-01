# OrderBook performance boost (2026-02-01)

## Scope
- Workload: add/delete 50% each, trade 10%
- Benchmark: `BM_OrderBookWorkload/ops:50000/trade_pct:10`
- Host: `sharp-game-fades-fin-03` (AMD EPYC 9654, KVM)

## Core code changes
1) Remove full-table scans on insert
- Replaced `hasEmptySlot` / `hasEmptyPriceSlot` with occupancy counters.
- Early full check now uses `order_count_ >= MAX_ORDERS` and `price_level_count_ >= MAX_PRICE_LEVELS`.
- Files:
  - `impl/include/impl/order_book.hpp`
  - `impl/src/order_book.cpp`

2) Cache PriceLevel pointer in each Order
- Added `Order::level` to avoid repeated hash lookups in `deleteOrder`, `modifyOrder`, `processTrade`.
- Files:
  - `impl/include/impl/order_book.hpp`
  - `impl/src/order_book.cpp`

3) Reduce object alignment inflation
- Removed `alignas(64)` from `Order` and `PriceLevel` to reduce cache footprint.
- File:
  - `impl/include/impl/order_book.hpp`

## Performance results
Baseline report: `perf-orderbook/orderbook_case_study_before.md`
Optimized report: `perf-orderbook/orderbook_case_study.md`

Throughput:
- Before: 28.416M ops/s
- After: 29.44M ops/s
- Change: +3.6%

Key counters (before -> after):
- Instructions: 15.144B -> 15.546B
- Cycles: 14.817B -> 15.026B
- IPC: 1.022 -> 1.035
- CPI: 0.978 -> 0.967
- Branch miss rate: 1.507% -> 1.567%
- Cache miss rate: 6.941% -> 4.618%
- L1D MPKI: 78.02 -> 92.18
- L1I MPKI: 0.049 -> 0.050
- dTLB MPKI: 0.022 -> 0.011
- iTLB MPKI: 0.002 -> 0.001

## Analysis
- Removing full-table scans eliminated an O(N) pass on every add, which directly reduced data-cache traffic on the hot path.
- Caching `PriceLevel*` in each `Order` removed one hash probe per delete/modify/trade, reducing pointer chasing.
- The overall cache miss rate dropped, but L1D MPKI increased. This likely reflects a different miss mix after removing scans and changing object alignment. The net effect is still positive on throughput.
- The bottleneck remains data access: IPC is just above 1 and L1D MPKI is still high for this workload.

## Next experiments (not yet run)
- Restore `alignas(64)` only for `Order`/`PriceLevel` and re-run to isolate its impact on L1D MPKI.
- Convert `order_map_` to store indices instead of pointers to reduce pointer-chasing.
- Run multiple repetitions to stabilize variance (e.g., `--benchmark_repetitions=5`).
