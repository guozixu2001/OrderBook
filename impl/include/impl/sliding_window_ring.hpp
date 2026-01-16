#pragma once

#include <array>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <climits>

// Branch prediction hints for compiler optimization
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace impl {

// Forward declaration
class OrderBook;

// ============================================================================
// RingBufferSlidingWindowStats: Optimized version using ring buffer + two heaps
// ============================================================================
// Key optimization: Running Median with Two Heaps
// - Left heap (max-heap): stores smaller half
// - Right heap (min-heap): stores larger half
// Complexity: recordTrade() O(log N), getMedianPrice() O(1), evictExpired() O(evicted log N)
// ============================================================================

class alignas(64) RingBufferSlidingWindowStats {
private:
  static constexpr size_t MAX_TRADES = 65536;      // Max trades in 10 minute window
  static constexpr size_t WINDOW_SECONDS = 600;    // 10 minutes
  static constexpr double REBUILD_THRESHOLD = 0.25; // Rebuild when >25% expired

  // Trade data (SoA layout for cache efficiency)
  alignas(64) std::array<uint64_t, MAX_TRADES> timestamps_;  // Trade timestamps
  alignas(64) std::array<int32_t, MAX_TRADES> prices_;       // Trade prices
  alignas(64) std::array<uint64_t, MAX_TRADES> quantities_;  // Trade quantities
  alignas(64) std::array<uint64_t, MAX_TRADES> amounts_;     // price * quantity

  // Ring buffer pointers
  size_t head_ = 0;    // Next write position
  size_t count_ = 0;   // Current number of trades in window

  // Incremental statistics
  uint64_t sum_qty_ = 0;      // Sum of quantities
  uint64_t sum_amount_ = 0;   // Sum of amounts (price * qty)

  // Cached min/max prices (rebuilt when invalidated)
  mutable int32_t cached_min_price_ = INT32_MAX;
  mutable int32_t cached_max_price_ = INT32_MIN;
  mutable bool cache_valid_ = true;

  // =========================================================================
  // Two Heap for Running Median (O(1) median query)
  // =========================================================================
  // Heaps store indices into the ring buffer (prices_ array)
  // Left heap is a max-heap (prices[left_heap_[i]] <= prices[left_heap_[parent]])
  // Right heap is a min-heap (prices[right_heap_[i]] >= prices[right_heap_[parent]])
  alignas(64) std::array<size_t, MAX_TRADES> left_heap_;   // Max-heap for smaller half
  alignas(64) std::array<size_t, MAX_TRADES> right_heap_;  // Min-heap for larger half
  size_t left_heap_size_ = 0;
  size_t right_heap_size_ = 0;

  // Track which heap each trade index is in, and if it's expired
  // -1 = not in heap, 0 = in left heap, 1 = in right heap
  alignas(64) std::array<int8_t, MAX_TRADES> trade_in_heap_;  // -1/0/1
  alignas(64) std::array<bool, MAX_TRADES> trade_expired_;    // true = marked for eviction

  // Heap helper functions (all operations are O(log N))
  void siftUpLeft(size_t index);      // Max-heap sift up
  void siftDownLeft(size_t index);    // Max-heap sift down
  void siftUpRight(size_t index);     // Min-heap sift up
  void siftDownRight(size_t index);   // Min-heap sift down
  void swapHeapNodes(size_t heap, size_t i, size_t j);  // Swap nodes in heap
  size_t pushToLeftHeap(size_t trade_idx);   // Insert into left heap, return position
  size_t pushToRightHeap(size_t trade_idx);  // Insert into right heap, return position
  void balanceHeaps();                 // Rebalance left/right sizes
  void cleanTopOfLeftHeap() const;           // Remove expired from left heap top
  void cleanTopOfRightHeap() const;          // Remove expired from right heap top

  // Private helper functions
  void rebuildCacheIfNeeded() const;
  void invalidateCache() { cache_valid_ = false; }

  // Helper to check if a trade is valid (not expired and in valid window)
  bool isTradeValid(size_t trade_idx) const {
    return trade_idx < MAX_TRADES &&
           timestamps_[trade_idx] != 0 &&
           !trade_expired_[trade_idx];
  }

public:
  RingBufferSlidingWindowStats();

  // Record a trade (O(log N) due to heap insert)
  void recordTrade(uint64_t timestamp, int32_t price, uint64_t qty);

  // Remove expired trades older than 10 minutes (O(k log N) where k = expired count)
  void evictExpired(uint64_t current_timestamp);

  // Query functions
  int32_t getPriceRange() const {
    if (count_ == 0) return 0;
    rebuildCacheIfNeeded();
    return cached_max_price_ - cached_min_price_;
  }

  uint64_t getTotalVolume() const { return sum_qty_; }
  uint64_t getTotalAmount() const { return sum_amount_; }

  uint64_t getVWAP() const {
    if (count_ == 0 || sum_qty_ == 0) return 0;
    return sum_amount_ / sum_qty_;
  }

  // Get median price using two heaps - O(1)
  int32_t getMedianPrice() const;

  // Get VWAP level (which price level the VWAP falls into)
  int32_t getVWAPLevel(const OrderBook* ob) const;

  // Debug/validation
  size_t getCount() const { return count_; }
  bool isCacheValid() const { return cache_valid_; }
  size_t getLeftHeapSize() const { return left_heap_size_; }
  size_t getRightHeapSize() const { return right_heap_size_; }
};

} // namespace impl
