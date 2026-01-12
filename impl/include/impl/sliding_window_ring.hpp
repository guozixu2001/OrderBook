#pragma once

#include <array>
#include <cstdint>
#include <algorithm>
#include <limits>

// Branch prediction hints for compiler optimization
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace impl {

// Forward declaration
class OrderBook;

// ============================================================================
// RingBufferSlidingWindowStats: Optimized version using ring buffer + cache
// ============================================================================
// This implementation avoids heap operations for min/max tracking.
// Instead, it maintains a cached min/max and rebuilds when invalidated.
// Complexity: recordTrade() O(1), getPriceRange() O(1) amortized
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

  // Pre-allocated cache for median calculation
  mutable std::array<int32_t, MAX_TRADES> price_cache_;

  // Private helper functions
  void rebuildCacheIfNeeded() const;
  void invalidateCache() { cache_valid_ = false; }

public:
  RingBufferSlidingWindowStats();

  // Record a trade (O(1))
  void recordTrade(uint64_t timestamp, int32_t price, uint64_t qty);

  // Remove expired trades older than 10 minutes (O(k) where k = expired count)
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

  // Get median price (O(n) but using pre-allocated cache)
  int32_t getMedianPrice() const;

  // Get VWAP level (which price level the VWAP falls into)
  int32_t getVWAPLevel(const OrderBook* ob) const;

  // Debug/validation
  size_t getCount() const { return count_; }
  bool isCacheValid() const { return cache_valid_; }
};

} // namespace impl
