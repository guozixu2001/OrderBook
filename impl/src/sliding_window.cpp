#include "impl/sliding_window.hpp"
#include "impl/order_book.hpp"

#include <ctime>
#include <climits>

namespace impl {

// Helper functions for timestamp conversion
static uint64_t nanosecondsToUnixSeconds(uint64_t nanoseconds) {
  return nanoseconds / 1000000000ULL;
}

static uint64_t yyyymmddhhmmssToUnixSeconds(uint64_t yyyymmddhhmmss) {
  // Extract components
  uint64_t sec = yyyymmddhhmmss % 100;
  uint64_t min = (yyyymmddhhmmss / 100) % 100;
  uint64_t hour = (yyyymmddhhmmss / 10000) % 100;
  uint64_t day = (yyyymmddhhmmss / 1000000) % 100;
  uint64_t month = (yyyymmddhhmmss / 100000000) % 100;
  uint64_t year = yyyymmddhhmmss / 10000000000;

  struct tm timeinfo = {};
  timeinfo.tm_sec = static_cast<int>(sec);
  timeinfo.tm_min = static_cast<int>(min);
  timeinfo.tm_hour = static_cast<int>(hour);
  timeinfo.tm_mday = static_cast<int>(day);
  timeinfo.tm_mon = static_cast<int>(month) - 1;  // tm_mon is 0-based (0=Jan)
  timeinfo.tm_year = static_cast<int>(year) - 1900;  // tm_year is years since 1900
  timeinfo.tm_isdst = 0;  // No daylight saving time (UTC)

  time_t timestamp = mktime(&timeinfo);
  if (timestamp == -1) {
    return 0;
  }
  return static_cast<uint64_t>(timestamp);
}

// ============================================================================
// SlidingWindowStats Heap Operations
// ============================================================================

void SlidingWindowStats::pushToMaxHeap(size_t trade_idx) {
  // Add to end of heap
  size_t pos = max_heap_size_;
  max_heap_[pos] = trade_idx;
  max_heap_pos_[trade_idx] = pos;
  max_heap_size_++;

  // Bubble up
  while (pos > 0) {
    size_t parent = (pos - 1) / 2;
    if (prices_[max_heap_[parent]] >= prices_[max_heap_[pos]]) {
      break;
    }
    // Swap with parent
    std::swap(max_heap_[parent], max_heap_[pos]);
    max_heap_pos_[max_heap_[parent]] = parent;
    max_heap_pos_[max_heap_[pos]] = pos;
    pos = parent;
  }
}

void SlidingWindowStats::pushToMinHeap(size_t trade_idx) {
  // Add to end of heap
  size_t pos = min_heap_size_;
  min_heap_[pos] = trade_idx;
  min_heap_pos_[trade_idx] = pos;
  min_heap_size_++;

  // Bubble up
  while (pos > 0) {
    size_t parent = (pos - 1) / 2;
    if (prices_[min_heap_[parent]] <= prices_[min_heap_[pos]]) {
      break;
    }
    // Swap with parent
    std::swap(min_heap_[parent], min_heap_[pos]);
    min_heap_pos_[min_heap_[parent]] = parent;
    min_heap_pos_[min_heap_[pos]] = pos;
    pos = parent;
  }
}

void SlidingWindowStats::removeFromMaxHeap(size_t trade_idx) {
  size_t pos = max_heap_pos_[trade_idx];
  if (pos == SIZE_MAX || pos >= max_heap_size_) {
    return;  // Not in heap
  }

  // Mark as removed
  max_heap_pos_[trade_idx] = SIZE_MAX;

  // If last element, just reduce size
  if (pos == max_heap_size_ - 1) {
    max_heap_size_--;
    return;
  }

  // Move last element to this position
  size_t last_idx = max_heap_[max_heap_size_ - 1];
  max_heap_[pos] = last_idx;
  max_heap_pos_[last_idx] = pos;
  max_heap_size_--;

  // Restore heap property
  size_t current = pos;
  while (true) {
    size_t left = 2 * current + 1;
    size_t right = 2 * current + 2;
    size_t largest = current;

    if (left < max_heap_size_ &&
        prices_[max_heap_[left]] > prices_[max_heap_[largest]]) {
      largest = left;
    }
    if (right < max_heap_size_ &&
        prices_[max_heap_[right]] > prices_[max_heap_[largest]]) {
      largest = right;
    }

    if (largest == current) {
      break;
    }

    std::swap(max_heap_[current], max_heap_[largest]);
    max_heap_pos_[max_heap_[current]] = current;
    max_heap_pos_[max_heap_[largest]] = largest;
    current = largest;
  }
}

void SlidingWindowStats::removeFromMinHeap(size_t trade_idx) {
  size_t pos = min_heap_pos_[trade_idx];
  if (pos == SIZE_MAX || pos >= min_heap_size_) {
    return;  // Not in heap
  }

  // Mark as removed
  min_heap_pos_[trade_idx] = SIZE_MAX;

  // If last element, just reduce size
  if (pos == min_heap_size_ - 1) {
    min_heap_size_--;
    return;
  }

  // Move last element to this position
  size_t last_idx = min_heap_[min_heap_size_ - 1];
  min_heap_[pos] = last_idx;
  min_heap_pos_[last_idx] = pos;
  min_heap_size_--;

  // Restore heap property
  size_t current = pos;
  while (true) {
    size_t left = 2 * current + 1;
    size_t right = 2 * current + 2;
    size_t smallest = current;

    if (left < min_heap_size_ &&
        prices_[min_heap_[left]] < prices_[min_heap_[smallest]]) {
      smallest = left;
    }
    if (right < min_heap_size_ &&
        prices_[min_heap_[right]] < prices_[min_heap_[smallest]]) {
      smallest = right;
    }

    if (smallest == current) {
      break;
    }

    std::swap(min_heap_[current], min_heap_[smallest]);
    min_heap_pos_[min_heap_[current]] = current;
    min_heap_pos_[min_heap_[smallest]] = smallest;
    current = smallest;
  }
}

void SlidingWindowStats::rebuildHeapsIfNeeded() {
  // Simple rebuild: if heap size is much smaller than count, rebuild
  if (max_heap_size_ < count_ / 2 || min_heap_size_ < count_ / 2) {
    // For now, just set flag to force update from heaps
    // In a more complete implementation, we would rebuild heaps
  }
}

void SlidingWindowStats::updateMinMaxFromHeaps() const {
  // Clean invalid elements from top of heaps
  while (max_heap_size_ > 0) {
    size_t top_idx = max_heap_[0];
    if (valid_[top_idx] && max_heap_pos_[top_idx] == 0) {
      break;  // Valid and at correct position
    }
    // Invalid or misplaced, remove it
    const_cast<SlidingWindowStats*>(this)->removeFromMaxHeap(top_idx);
  }

  while (min_heap_size_ > 0) {
    size_t top_idx = min_heap_[0];
    if (valid_[top_idx] && min_heap_pos_[top_idx] == 0) {
      break;  // Valid and at correct position
    }
    const_cast<SlidingWindowStats*>(this)->removeFromMinHeap(top_idx);
  }

  // Update min/max prices
  if (max_heap_size_ > 0) {
    max_price_ = prices_[max_heap_[0]];
  } else {
    max_price_ = INT32_MIN;
  }

  if (min_heap_size_ > 0) {
    min_price_ = prices_[min_heap_[0]];
  } else {
    min_price_ = INT32_MAX;
  }
}

// ============================================================================
// SlidingWindowStats Public Interface
// ============================================================================

SlidingWindowStats::SlidingWindowStats() {
  sec_index_.fill(SIZE_MAX);
  sec_count_.fill(0);

  // Initialize heap data structures
  max_heap_pos_.fill(SIZE_MAX);
  min_heap_pos_.fill(SIZE_MAX);
  valid_.fill(false);  // No trades recorded yet
}

void SlidingWindowStats::recordTrade(uint64_t timestamp_ns, int32_t price, uint64_t qty) {
  // Convert nanoseconds to Unix seconds
  uint64_t timestamp = nanosecondsToUnixSeconds(timestamp_ns);

  // Calculate amount
  uint64_t amount = static_cast<uint64_t>(price) * qty;

  // Store in ring buffer
  size_t idx = head_;

  // If this position already has a valid trade (buffer full), remove it from heaps
  if (valid_[idx]) {
    removeFromMaxHeap(idx);
    removeFromMinHeap(idx);
  }

  timestamps_[idx] = timestamp;
  prices_[idx] = price;
  quantities_[idx] = qty;
  amounts_[idx] = amount;

  // Mark as valid and add to heaps
  valid_[idx] = true;
  pushToMaxHeap(idx);
  pushToMinHeap(idx);

  // Update incremental statistics
  sum_qty_ += qty;
  sum_amount_ += amount;

  // Update secondary index
  if (base_timestamp_ == 0) {
    base_timestamp_ = timestamp;
  }
  size_t bucket_idx = (timestamp - base_timestamp_) % SECONDARY_BUCKETS;
  if (sec_index_[bucket_idx] == SIZE_MAX) {
    sec_index_[bucket_idx] = idx;
    sec_count_[bucket_idx] = 1;
  } else {
    sec_count_[bucket_idx]++;
  }

  // Advance head
  head_ = (head_ + 1) % MAX_TRADES;
  if (count_ < MAX_TRADES) {
    count_++;
  }
}

void SlidingWindowStats::evictExpired(uint64_t current_timestamp_yyyymmddhhmmss) {
  // Convert grid time (YYYYMMDDHHMMSS) to Unix seconds
  uint64_t current_seconds = yyyymmddhhmmssToUnixSeconds(current_timestamp_yyyymmddhhmmss);

  // Calculate cutoff time: 10 minutes (600 seconds) before current time
  uint64_t cutoff_seconds = current_seconds - WINDOW_SECONDS;

  // Use secondary index for efficient eviction
  // We need to evict trades that are either:
  // 1. Too old: timestamp < cutoff_seconds
  // 2. Too new: timestamp >= current_seconds (current second)
  while (count_ > 0) {
    size_t tail_idx = (head_ + (MAX_TRADES - count_)) % MAX_TRADES;
    uint64_t tail_time = timestamps_[tail_idx];  // tail_time is in Unix seconds

    // Window: [cutoff_seconds, current_seconds) includes cutoff_seconds, excludes current_seconds.
    // In other words, include data from 10 minutes ago up to (but not including) the current second.
    if (tail_time >= cutoff_seconds && tail_time < current_seconds) {
      break;  // All remaining trades are within window
    }

    // Evict this trade
    uint64_t qty = quantities_[tail_idx];
    uint64_t amount = amounts_[tail_idx];
    // Note: price is not used here - handled by heap maintenance

    sum_qty_ -= qty;
    sum_amount_ -= amount;

    // Remove from heaps if valid
    if (valid_[tail_idx]) {
      removeFromMaxHeap(tail_idx);
      removeFromMinHeap(tail_idx);
      valid_[tail_idx] = false;
    }

    // Update secondary index
    if (base_timestamp_ > 0) {
      size_t bucket_idx = (tail_time - base_timestamp_) % SECONDARY_BUCKETS;
      if (sec_count_[bucket_idx] > 0) {
        sec_count_[bucket_idx]--;
        if (sec_count_[bucket_idx] == 0) {
          sec_index_[bucket_idx] = SIZE_MAX;
        }
      }
    }

    count_--;
  }

  // min/max are updated lazily via updateMinMaxFromHeaps() when getPriceRange() is called
}

int32_t SlidingWindowStats::getMedianPrice() const {
  if (count_ == 0) return 0;

  // Copy prices to cache
  size_t num_prices = std::min(count_, MAX_TRADES);
  for (size_t i = 0; i < num_prices; i++) {
    size_t idx = (head_ + (MAX_TRADES - count_ + i)) % MAX_TRADES;
    price_cache_[i] = prices_[idx];
  }

  // Use quickselect for O(n) median
  size_t mid = num_prices / 2;
  std::nth_element(price_cache_.begin(),
                   price_cache_.begin() + mid,
                   price_cache_.begin() + num_prices);

  if (num_prices % 2 == 0) {
    // Even number: average of two middle values
    int32_t mid1 = price_cache_[mid];
    std::nth_element(price_cache_.begin(),
                     price_cache_.begin() + mid - 1,
                     price_cache_.begin() + mid);
    int32_t mid0 = price_cache_[mid - 1];
    return (mid0 + mid1) / 2;
  } else {
    // Odd number
    return price_cache_[mid];
  }
}

int32_t SlidingWindowStats::getVWAPLevel(const OrderBook* ob) const {
  uint64_t vwap = getVWAP();
  if (vwap == 0 || !ob) return 0;

  int32_t vwap_price = static_cast<int32_t>(vwap);

  // 1. Check the ask side (VWAP in ask region: >= best ask).
  if (ob->getAskLevels() > 0) {
    int32_t best_ask = ob->getAskPrice(0);
    if (vwap_price >= best_ask) {
      size_t ask_levels = ob->getAskLevels();
      for (size_t i = 0; i < ask_levels; i++) {
        int32_t price = ob->getAskPrice(i);
        // Ask logic: find the level containing VWAP (vwap <= ask_price).
        if (vwap_price <= price) {
          return static_cast<int32_t>(-static_cast<int32_t>(i));
        }
      }
      // If VWAP is above the worst ask, return 0 (or define an out-of-book value).
      return 0;
    }
  }

  // 2. Check the bid side (VWAP in bid region: <= best bid).
  if (ob->getBidLevels() > 0) {
    int32_t best_bid = ob->getBidPrice(0);
    if (vwap_price <= best_bid) {
      size_t bid_levels = ob->getBidLevels();
      for (size_t i = 0; i < bid_levels; i++) {
        int32_t price = ob->getBidPrice(i);
        // Bid logic: find the level containing VWAP (vwap >= bid_price).
        if (vwap_price >= price) {
          return static_cast<int32_t>(i);
        }
      }
    }
  }

  return 0;
}

} // namespace impl
