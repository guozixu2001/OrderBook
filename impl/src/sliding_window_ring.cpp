#include "impl/sliding_window_ring.hpp"
#include "impl/order_book.hpp"

#include <ctime>
#include <limits>

namespace impl {

// ============================================================================
// RingBufferSlidingWindowStats Implementation with Two Heaps
// ============================================================================

RingBufferSlidingWindowStats::RingBufferSlidingWindowStats() {
  // Initialize caches
  cached_min_price_ = std::numeric_limits<int32_t>::max();
  cached_max_price_ = std::numeric_limits<int32_t>::min();
  cache_valid_ = true;

  // Initialize heap tracking arrays
  left_heap_.fill(0);
  right_heap_.fill(0);
  left_heap_size_ = 0;
  right_heap_size_ = 0;
  trade_in_heap_.fill(-1);
  trade_expired_.fill(false);
}

// ============================================================================
// Heap Operations (O(log N))
// ============================================================================

void RingBufferSlidingWindowStats::swapHeapNodes(size_t heap, size_t i, size_t j) {
  if (i == j) return;

  size_t idx_i = (heap == 0) ? left_heap_[i] : right_heap_[i];
  size_t idx_j = (heap == 0) ? left_heap_[j] : right_heap_[j];

  // Swap in the heap array
  if (heap == 0) {
    std::swap(left_heap_[i], left_heap_[j]);
  } else {
    std::swap(right_heap_[i], right_heap_[j]);
  }

  // Update tracking arrays - idx_i was at position j, now at position i
  // We need to update trade_in_heap_ to reflect their heap positions
  // Actually, we don't track heap positions in trade_in_heap_, just which heap
  // The heap positions are implicit in the array layout
}

void RingBufferSlidingWindowStats::siftUpLeft(size_t index) {
  // Max-heap: parent value >= child value
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (prices_[left_heap_[parent]] >= prices_[left_heap_[index]]) {
      break;
    }
    // Swap with parent
    std::swap(left_heap_[parent], left_heap_[index]);
    index = parent;
  }
}

void RingBufferSlidingWindowStats::siftDownLeft(size_t index) {
  // Max-heap: parent value >= child value
  size_t size = left_heap_size_;
  while (true) {
    size_t largest = index;
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;

    if (left < size && prices_[left_heap_[left]] > prices_[left_heap_[largest]]) {
      largest = left;
    }
    if (right < size && prices_[right_heap_[right]] > prices_[left_heap_[largest]]) {
      largest = right;
    }
    if (largest == index) break;

    std::swap(left_heap_[index], left_heap_[largest]);
    index = largest;
  }
}

void RingBufferSlidingWindowStats::siftUpRight(size_t index) {
  // Min-heap: parent value <= child value
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (prices_[right_heap_[parent]] <= prices_[right_heap_[index]]) {
      break;
    }
    // Swap with parent
    std::swap(right_heap_[parent], right_heap_[index]);
    index = parent;
  }
}

void RingBufferSlidingWindowStats::siftDownRight(size_t index) {
  // Min-heap: parent value <= child value
  size_t size = right_heap_size_;
  while (true) {
    size_t smallest = index;
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;

    if (left < size && prices_[right_heap_[left]] < prices_[right_heap_[smallest]]) {
      smallest = left;
    }
    if (right < size && prices_[right_heap_[right]] < prices_[right_heap_[smallest]]) {
      smallest = right;
    }
    if (smallest == index) break;

    std::swap(right_heap_[index], right_heap_[smallest]);
    index = smallest;
  }
}

size_t RingBufferSlidingWindowStats::pushToLeftHeap(size_t trade_idx) {
  size_t pos = left_heap_size_;
  left_heap_[pos] = trade_idx;
  left_heap_size_++;
  trade_in_heap_[trade_idx] = 0;  // 0 = left heap
  siftUpLeft(pos);
  return pos;
}

size_t RingBufferSlidingWindowStats::pushToRightHeap(size_t trade_idx) {
  size_t pos = right_heap_size_;
  right_heap_[pos] = trade_idx;
  right_heap_size_++;
  trade_in_heap_[trade_idx] = 1;  // 1 = right heap
  siftUpRight(pos);
  return pos;
}

void RingBufferSlidingWindowStats::balanceHeaps() {
  // Ensure left heap has at least as many elements as right heap
  // and size difference is at most 1
  while (left_heap_size_ > right_heap_size_ + 1) {
    // Move top of left heap to right heap
    size_t top_idx = left_heap_[0];
    left_heap_[0] = left_heap_[left_heap_size_ - 1];
    left_heap_size_--;
    siftDownLeft(0);

    // Push to right heap
    pushToRightHeap(top_idx);
  }

  while (right_heap_size_ > left_heap_size_) {
    // Move top of right heap to left heap
    size_t top_idx = right_heap_[0];
    right_heap_[0] = right_heap_[right_heap_size_ - 1];
    right_heap_size_--;
    siftDownRight(0);

    // Push to left heap
    pushToLeftHeap(top_idx);
  }

  // Also ensure all left elements <= all right elements
  if (left_heap_size_ > 0 && right_heap_size_ > 0) {
    int32_t left_max = prices_[left_heap_[0]];
    int32_t right_min = prices_[right_heap_[0]];

    if (left_max > right_min) {
      // Swap tops
      size_t left_top = left_heap_[0];
      size_t right_top = right_heap_[0];

      // Remove tops
      left_heap_[0] = left_heap_[left_heap_size_ - 1];
      left_heap_size_--;
      siftDownLeft(0);

      right_heap_[0] = right_heap_[right_heap_size_ - 1];
      right_heap_size_--;
      siftDownRight(0);

      // Reinsert in opposite heaps
      pushToRightHeap(left_top);
      pushToLeftHeap(right_top);
    }
  }
}

void RingBufferSlidingWindowStats::cleanTopOfLeftHeap() const {
  while (left_heap_size_ > 0) {
    size_t top_idx = left_heap_[0];
    if (isTradeValid(top_idx)) {
      return;  // Valid top
    }
    // Remove expired top
    const_cast<RingBufferSlidingWindowStats*>(this)->left_heap_[0] = left_heap_[left_heap_size_ - 1];
    const_cast<RingBufferSlidingWindowStats*>(this)->left_heap_size_--;
    if (left_heap_size_ > 0) {
      const_cast<RingBufferSlidingWindowStats*>(this)->siftDownLeft(0);
    }
    const_cast<RingBufferSlidingWindowStats*>(this)->trade_in_heap_[top_idx] = -1;
  }
}

void RingBufferSlidingWindowStats::cleanTopOfRightHeap() const {
  while (right_heap_size_ > 0) {
    size_t top_idx = right_heap_[0];
    if (isTradeValid(top_idx)) {
      return;  // Valid top
    }
    // Remove expired top
    const_cast<RingBufferSlidingWindowStats*>(this)->right_heap_[0] = right_heap_[right_heap_size_ - 1];
    const_cast<RingBufferSlidingWindowStats*>(this)->right_heap_size_--;
    if (right_heap_size_ > 0) {
      const_cast<RingBufferSlidingWindowStats*>(this)->siftDownRight(0);
    }
    const_cast<RingBufferSlidingWindowStats*>(this)->trade_in_heap_[top_idx] = -1;
  }
}

// ============================================================================
// Public Interface
// ============================================================================

void RingBufferSlidingWindowStats::recordTrade(uint64_t timestamp_ns, int32_t price, uint64_t qty) {
  // Convert nanoseconds to Unix seconds
  uint64_t timestamp = timestamp_ns / 1000000000ULL;

  uint64_t amount = static_cast<uint64_t>(price) * qty;

  size_t idx = head_;

  // Store in ring buffer
  timestamps_[idx] = timestamp;
  prices_[idx] = price;
  quantities_[idx] = qty;
  amounts_[idx] = amount;

  // Update incremental statistics
  sum_qty_ += qty;
  sum_amount_ += amount;

  // Update cached min/max (O(1))
  if (price < cached_min_price_) cached_min_price_ = price;
  if (price > cached_max_price_) cached_max_price_ = price;

  // Insert into appropriate heap for median calculation
  if (left_heap_size_ == 0 ||
      (left_heap_size_ > 0 && price <= prices_[left_heap_[0]])) {
    pushToLeftHeap(idx);
  } else {
    pushToRightHeap(idx);
  }
  balanceHeaps();

  // Advance head - since MAX_TRADES is power of 2, use bitwise AND
  head_ = (head_ + 1) & (MAX_TRADES - 1);
  if (count_ < MAX_TRADES) {
    count_++;
  }
}

void RingBufferSlidingWindowStats::evictExpired(uint64_t current_timestamp_yyyymmddhhmmss) {
  // Convert grid time (YYYYMMDDHHMMSS) to Unix seconds
  uint64_t sec = current_timestamp_yyyymmddhhmmss % 100;
  uint64_t min = (current_timestamp_yyyymmddhhmmss / 100) % 100;
  uint64_t hour = (current_timestamp_yyyymmddhhmmss / 10000) % 100;
  uint64_t day = (current_timestamp_yyyymmddhhmmss / 1000000) % 100;
  uint64_t month = (current_timestamp_yyyymmddhhmmss / 100000000) % 100;
  uint64_t year = current_timestamp_yyyymmddhhmmss / 10000000000;

  struct tm timeinfo = {};
  timeinfo.tm_sec = static_cast<int>(sec);
  timeinfo.tm_min = static_cast<int>(min);
  timeinfo.tm_hour = static_cast<int>(hour);
  timeinfo.tm_mday = static_cast<int>(day);
  timeinfo.tm_mon = static_cast<int>(month) - 1;
  timeinfo.tm_year = static_cast<int>(year) - 1900;
  timeinfo.tm_isdst = 0;

  time_t current_ts = mktime(&timeinfo);
  if (current_ts == -1) {
    return;
  }

  uint64_t current_seconds = static_cast<uint64_t>(current_ts);
  uint64_t cutoff_seconds = current_seconds - WINDOW_SECONDS;

  size_t evicted = 0;

  while (count_ > 0) {
    // Since MAX_TRADES is power of 2, use bitwise AND instead of modulo
    size_t tail_idx = (head_ + (MAX_TRADES - count_)) & (MAX_TRADES - 1);
    uint64_t tail_time = timestamps_[tail_idx];

    // Window: [cutoff_seconds, current_seconds) - contains cutoff, excludes current
    if (tail_time >= cutoff_seconds && tail_time < current_seconds) {
      break;
    }

    // Evict this trade
    sum_qty_ -= quantities_[tail_idx];
    sum_amount_ -= amounts_[tail_idx];
    count_--;

    // Mark as expired - will be lazily removed from heaps
    if (trade_in_heap_[tail_idx] != -1) {
      trade_expired_[tail_idx] = true;
      trade_in_heap_[tail_idx] = -1;
    }

    evicted++;
  }

  // Mark cache as invalid since min/max may have expired
  cache_valid_ = false;
}

int32_t RingBufferSlidingWindowStats::getMedianPrice() const {
  if (left_heap_size_ == 0 && right_heap_size_ == 0) {
    return 0;
  }

  // Clean expired elements from tops
  cleanTopOfLeftHeap();
  cleanTopOfRightHeap();

  // Rebalance if needed after cleaning
  // Note: This is a const_cast hack to call non-const balanceHeaps
  // In production, we should make balanceHeaps const or redesign
  const_cast<RingBufferSlidingWindowStats*>(this)->balanceHeaps();

  // Clean again after potential rebalancing
  cleanTopOfLeftHeap();
  cleanTopOfRightHeap();

  if (left_heap_size_ == 0 && right_heap_size_ == 0) {
    return 0;
  }

  // Calculate median from heap tops
  if (left_heap_size_ > right_heap_size_) {
    return prices_[left_heap_[0]];
  } else if (right_heap_size_ > left_heap_size_) {
    return prices_[right_heap_[0]];
  } else {
    // Even number of elements - average of two middle values
    int32_t left_max = prices_[left_heap_[0]];
    int32_t right_min = prices_[right_heap_[0]];
    return (left_max + right_min) / 2;
  }
}

void RingBufferSlidingWindowStats::rebuildCacheIfNeeded() const {
  if (cache_valid_) {
    return;  // Cache is up to date
  }

  // Rebuild by scanning the ring buffer
  cached_min_price_ = std::numeric_limits<int32_t>::max();
  cached_max_price_ = std::numeric_limits<int32_t>::min();

  size_t num_prices = std::min(count_, MAX_TRADES);
  // Since MAX_TRADES is power of 2, use bitwise AND instead of modulo
  size_t mask = MAX_TRADES - 1;
  for (size_t i = 0; i < num_prices; i++) {
    size_t idx = (head_ + (MAX_TRADES - count_ + i)) & mask;
    int32_t price = prices_[idx];
    if (price < cached_min_price_) cached_min_price_ = price;
    if (price > cached_max_price_) cached_max_price_ = price;
  }

  cache_valid_ = true;
}

int32_t RingBufferSlidingWindowStats::getVWAPLevel(const OrderBook* ob) const {
  uint64_t vwap = getVWAP();
  if (vwap == 0 || !ob) return 0;

  int32_t vwap_price = static_cast<int32_t>(vwap);

  // Check Ask side
  if (ob->getAskLevels() > 0) {
    int32_t best_ask = ob->getAskPrice(0);
    if (vwap_price >= best_ask) {
      size_t ask_levels = ob->getAskLevels();
      for (size_t i = 0; i < ask_levels; i++) {
        int32_t price = ob->getAskPrice(i);
        if (vwap_price <= price) {
          return static_cast<int32_t>(-static_cast<int32_t>(i));
        }
      }
      return 0;
    }
  }

  // Check Bid side
  if (ob->getBidLevels() > 0) {
    int32_t best_bid = ob->getBidPrice(0);
    if (vwap_price <= best_bid) {
      size_t bid_levels = ob->getBidLevels();
      for (size_t i = 0; i < bid_levels; i++) {
        int32_t price = ob->getBidPrice(i);
        if (vwap_price >= price) {
          return static_cast<int32_t>(i);
        }
      }
    }
  }

  return 0;
}

} // namespace impl
