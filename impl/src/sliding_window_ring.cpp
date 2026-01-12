#include "impl/sliding_window_ring.hpp"
#include "impl/order_book.hpp"

#include <ctime>
#include <limits>

namespace impl {

// ============================================================================
// RingBufferSlidingWindowStats Implementation
// ============================================================================

RingBufferSlidingWindowStats::RingBufferSlidingWindowStats() {
  // Initialize caches
  cached_min_price_ = std::numeric_limits<int32_t>::max();
  cached_max_price_ = std::numeric_limits<int32_t>::min();
  cache_valid_ = true;
}

void RingBufferSlidingWindowStats::rebuildCacheIfNeeded() const {
  if (cache_valid_) {
    return;  // Cache is up to date
  }

  // Rebuild by scanning the ring buffer
  cached_min_price_ = std::numeric_limits<int32_t>::max();
  cached_max_price_ = std::numeric_limits<int32_t>::min();

  size_t num_prices = std::min(count_, MAX_TRADES);
  for (size_t i = 0; i < num_prices; i++) {
    size_t idx = (head_ + (MAX_TRADES - count_ + i)) % MAX_TRADES;
    int32_t price = prices_[idx];
    if (price < cached_min_price_) cached_min_price_ = price;
    if (price > cached_max_price_) cached_max_price_ = price;
  }

  cache_valid_ = true;
}

void RingBufferSlidingWindowStats::recordTrade(uint64_t timestamp, int32_t price, uint64_t qty) {
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

  // Advance head
  head_ = (head_ + 1) % MAX_TRADES;
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

  size_t old_count = count_;
  size_t evicted = 0;

  while (count_ > 0) {
    size_t tail_idx = (head_ + (MAX_TRADES - count_)) % MAX_TRADES;
    uint64_t tail_time = timestamps_[tail_idx];

    // Window: [cutoff_seconds, current_seconds)
    if (tail_time >= cutoff_seconds && tail_time < current_seconds) {
      break;
    }

    // Evict this trade
    sum_qty_ -= quantities_[tail_idx];
    sum_amount_ -= amounts_[tail_idx];
    count_--;
    evicted++;
  }

  // Threshold-based rebuild: rebuild when >25% expired
  if (old_count > 0 && static_cast<double>(evicted) / old_count > REBUILD_THRESHOLD) {
    cache_valid_ = false;
    rebuildCacheIfNeeded();
  } else {
    // Mark cache as invalid (expired data may have contained min/max)
    cache_valid_ = false;
  }
}

int32_t RingBufferSlidingWindowStats::getMedianPrice() const {
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
