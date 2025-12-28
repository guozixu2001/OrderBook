#include "impl/order_book.hpp"

#include <algorithm>
#include <cmath>

namespace impl {

// Constructor
OrderBook::OrderBook(const char* symbol) {
  std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
  symbol_[SYMBOL_LEN - 1] = '\0';
  order_map_.fill(nullptr);
  price_level_map_.fill(nullptr);
  bbo_ = {};

  // Allocate memory pools on heap (during initialization, not hot path)
  order_pool_ = new MemoryPool<Order, MAX_ORDERS>();
  level_pool_ = new MemoryPool<PriceLevel, MAX_PRICE_LEVELS>();
}

// Destructor
OrderBook::~OrderBook() {
  // Clear all orders and price levels
  clear();

  // Delete memory pools
  delete order_pool_;
  delete level_pool_;
}

// Convert price to hash index
size_t OrderBook::priceToIndex(int32_t price) const {
  // For negative prices (rare), take absolute value
  uint32_t abs_price = static_cast<uint32_t>(std::abs(price));
  return abs_price % MAX_PRICE_LEVELS;
}

// Find a price level by price (O(1) using hash map)
PriceLevel* OrderBook::findPriceLevel(int32_t price) const {
  size_t index = priceToIndex(price);
  PriceLevel* level = price_level_map_[index];

  // Verify the level matches the price (handle hash collisions)
  if (level && level->price == price) {
    return level;
  }
  return nullptr;
}

// Add a price level to the sorted list and hash map
void OrderBook::addPriceLevel(PriceLevel* new_level) {
  Side side = new_level->side;
  PriceLevel** head = (side == Side::BUY) ? &bids_ : &asks_;

  // Add to hash map
  size_t index = priceToIndex(new_level->price);
  price_level_map_[index] = new_level;

  if (!*head) {
    // First level
    *head = new_level;
    new_level->prev = new_level;
    new_level->next = new_level;
    return;
  }

  PriceLevel* current = *head;

  if (side == Side::BUY) {
    // Bids: sorted descending (best bid is highest price)
    if (new_level->price > current->price) {
      // New best bid
      new_level->prev = current->prev;
      new_level->next = current;
      current->prev->next = new_level;
      current->prev = new_level;
      *head = new_level;
      return;
    }

    while (current->next != *head && current->next->price > new_level->price) {
      current = current->next;
    }
  } else {
    // Asks: sorted ascending (best ask is lowest price)
    if (new_level->price < current->price) {
      // New best ask
      new_level->prev = current->prev;
      new_level->next = current;
      current->prev->next = new_level;
      current->prev = new_level;
      *head = new_level;
      return;
    }

    while (current->next != *head && current->next->price < new_level->price) {
      current = current->next;
    }
  }

  // Insert after current
  new_level->prev = current;
  new_level->next = current->next;
  current->next->prev = new_level;
  current->next = new_level;
}

// Remove a price level from the list and hash map
void OrderBook::removePriceLevel(PriceLevel* level) {
  Side side = level->side;
  PriceLevel** head = (side == Side::BUY) ? &bids_ : &asks_;

  // Remove from hash map
  size_t index = priceToIndex(level->price);
  price_level_map_[index] = nullptr;

  if (level->next == level) {
    // Only one level
    *head = nullptr;
  } else {
    level->prev->next = level->next;
    level->next->prev = level->prev;
    if (*head == level) {
      *head = level->next;
    }
  }

  level_pool_->deallocate(level);
}

// Update Best Bid/Offer (full update)
void OrderBook::updateBBO() {
  if (bids_) {
    bbo_.bid_price = bids_->price;
    bbo_.bid_qty = bids_->total_qty;
  } else {
    bbo_.bid_price = 0;
    bbo_.bid_qty = 0;
  }

  if (asks_) {
    bbo_.ask_price = asks_->price;
    bbo_.ask_qty = asks_->total_qty;
  } else {
    bbo_.ask_price = 0;
    bbo_.ask_qty = 0;
  }
}

// Update BBO only for specific side when needed
void OrderBook::updateBBOSide(bool update_bid, bool update_ask) {
  if (update_bid) {
    if (bids_) {
      bbo_.bid_price = bids_->price;
      bbo_.bid_qty = bids_->total_qty;
    } else {
      bbo_.bid_price = 0;
      bbo_.bid_qty = 0;
    }
  }

  if (update_ask) {
    if (asks_) {
      bbo_.ask_price = asks_->price;
      bbo_.ask_qty = asks_->total_qty;
    } else {
      bbo_.ask_price = 0;
      bbo_.ask_qty = 0;
    }
  }
}

// Clear entire order book
void OrderBook::clear() {
  // Clear all orders
  for (Order* order : order_map_) {
    if (order) {
      order_pool_->deallocate(order);
    }
  }
  order_map_.fill(nullptr);

  // Clear price level hash map
  price_level_map_.fill(nullptr);

  // Clear all price levels
  while (bids_) {
    PriceLevel* next = bids_->next;
    if (next == bids_) {
      level_pool_->deallocate(bids_);
      bids_ = nullptr;
    } else {
      bids_->prev->next = bids_->next;
      bids_->next->prev = bids_->prev;
      PriceLevel* to_delete = bids_;
      bids_ = next;
      level_pool_->deallocate(to_delete);
    }
  }

  while (asks_) {
    PriceLevel* next = asks_->next;
    if (next == asks_) {
      level_pool_->deallocate(asks_);
      asks_ = nullptr;
    } else {
      asks_->prev->next = asks_->next;
      asks_->next->prev = asks_->prev;
      PriceLevel* to_delete = asks_;
      asks_ = next;
      level_pool_->deallocate(to_delete);
    }
  }

  // Reset sliding window statistics
  // We need to reconstruct it since there's no clear() method
  window_stats_ = SlidingWindowStats();

  updateBBO();
}

// Add a new order
void OrderBook::addOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  // Check if order_id is valid
  size_t map_index = order_id % MAX_ORDERS;
  if (order_map_[map_index] != nullptr) {
    // Order ID already exists
    return;
  }

  // Create new order from pool
  Order* order = order_pool_->allocate(order_id, price, qty, side);
  if (!order) return;  // Pool exhausted

  // Store in order map
  order_map_[map_index] = order;

  // Check if BBO needs updating
  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    update_bid = (bids_ && price >= bids_->price) || !bids_;
  } else {
    update_ask = (asks_ && price <= asks_->price) || !asks_;
  }

  // Find or create price level
  PriceLevel* level = findPriceLevel(price);
  if (!level) {
    level = level_pool_->allocate(price, side, order);
    if (!level) {
      order_pool_->deallocate(order);
      order_map_[map_index] = nullptr;
      return;
    }
    addPriceLevel(level);
  } else {
    // Add to existing price level
    Order* first = level->first_order;
    order->prev = first->prev;
    order->next = first;
    first->prev->next = order;
    first->prev = order;
    level->total_qty += qty;
    level->order_count++;
  }

  updateBBOSide(update_bid, update_ask);
}

// Modify an existing order
void OrderBook::modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  size_t map_index = order_id % MAX_ORDERS;
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
    return;  // Order not found
  }

  // Check if BBO needs updating (price change could affect BBO)
  bool update_bid = false;
  bool update_ask = false;

  if (order->price != price) {
    // Price changed - check if old or new price affects BBO
    if (side == Side::BUY) {
      update_bid = (bids_ && (order->price == bids_->price || price >= bids_->price));
    } else {
      update_ask = (asks_ && (order->price == asks_->price || price <= asks_->price));
    }

    // Delete and re-add with new price
    deleteOrder(order_id, side);
    addOrder(order_id, price, qty, side);
  } else {
    // Update quantity at same price
    PriceLevel* level = findPriceLevel(price);
    if (level) {
      int32_t qty_diff = static_cast<int32_t>(qty) - static_cast<int32_t>(order->qty);
      order->qty = qty;
      level->total_qty += qty_diff;

      // If this level is BBO, need to update
      if (side == Side::BUY && bids_ && bids_->price == price) {
        update_bid = true;
      } else if (side == Side::SELL && asks_ && asks_->price == price) {
        update_ask = true;
      }
    }
  }

  updateBBOSide(update_bid, update_ask);
}

// Delete an order
void OrderBook::deleteOrder(uint64_t order_id, Side side) {
  size_t map_index = order_id % MAX_ORDERS;
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
    return;  // Order not found
  }

  // Check if BBO needs updating (deleting from BBO level)
  bool update_bid = false;
  bool update_ask = false;

  PriceLevel* level = findPriceLevel(order->price);
  if (!level) return;

  if (side == Side::BUY && bids_ && bids_->price == order->price) {
    update_bid = true;
  } else if (side == Side::SELL && asks_ && asks_->price == order->price) {
    update_ask = true;
  }

  // Remove from price level
  if (order->next == order) {
    // Only order at this level
    removePriceLevel(level);
    level = nullptr;
  } else {
    order->prev->next = order->next;
    order->next->prev = order->prev;
    level->total_qty -= order->qty;
    level->order_count--;

    if (level->first_order == order) {
      level->first_order = order->next;
    }
  }

  // Remove from order map and deallocate
  order_map_[map_index] = nullptr;
  order_pool_->deallocate(order);

  updateBBOSide(update_bid, update_ask);
}

// Process a trade
void OrderBook::processTrade(uint64_t order_id, uint64_t /*trade_id*/, int32_t price,
                             uint64_t qty, Side side, uint64_t timestamp) {
  size_t map_index = order_id % MAX_ORDERS;
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
    return;  // Order not found
  }

  // Record trade for time window statistics
  window_stats_.recordTrade(timestamp, price, qty);
  window_stats_.evictExpired(timestamp);

  // Check if BBO needs updating (trade on BBO level)
  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY && bids_ && bids_->price == price) {
    update_bid = true;
  } else if (side == Side::SELL && asks_ && asks_->price == price) {
    update_ask = true;
  }

  if (order->qty <= qty) {
    deleteOrder(order_id, side);
  } else {
    // Partial fill
    order->qty -= qty;
    PriceLevel* level = findPriceLevel(price);
    if (level) {
      level->total_qty -= qty;
    }
  }

  updateBBOSide(update_bid, update_ask);
}

// Get number of bid levels
size_t OrderBook::getBidLevels() const {
  size_t count = 0;
  PriceLevel* current = bids_;
  while (current) {
    count++;
    if (current->next == bids_) break;
    current = current->next;
  }
  return count;
}

// Get number of ask levels
size_t OrderBook::getAskLevels() const {
  size_t count = 0;
  PriceLevel* current = asks_;
  while (current) {
    count++;
    if (current->next == asks_) break;
    current = current->next;
  }
  return count;
}

// Get bid price at level
int32_t OrderBook::getBidPrice(size_t level) const {
  PriceLevel* current = bids_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == bids_) return 0;
  }
  return current ? current->price : 0;
}

// Get bid quantity at level
uint32_t OrderBook::getBidQty(size_t level) const {
  PriceLevel* current = bids_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == bids_) return 0;
  }
  return current ? current->total_qty : 0;
}

// Get ask price at level
int32_t OrderBook::getAskPrice(size_t level) const {
  PriceLevel* current = asks_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == asks_) return 0;
  }
  return current ? current->price : 0;
}

// Get ask quantity at level
uint32_t OrderBook::getAskQty(size_t level) const {
  PriceLevel* current = asks_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == asks_) return 0;
  }
  return current ? current->total_qty : 0;
}

// Calculate mid price
double OrderBook::getMidPrice() const {
  if (bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    return (static_cast<double>(bbo_.bid_price) + bbo_.ask_price) / 2.0;
  }
  return 0.0;
}

// Calculate spread
int32_t OrderBook::getSpread() const {
  if (bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    return bbo_.ask_price - bbo_.bid_price;
  }
  return 0;
}

// Calculate macro price (volume-weighted mid price)
double OrderBook::getMacroPrice() const {
  if (bbo_.bid_qty > 0 && bbo_.ask_qty > 0 && bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    double bid_weight = static_cast<double>(bbo_.bid_qty);
    double ask_weight = static_cast<double>(bbo_.ask_qty);
    return (bbo_.ask_price * bid_weight + bbo_.bid_price * ask_weight) /
           (bid_weight + ask_weight);
  }
  return getMidPrice();
}

// Calculate order book imbalance for K levels
double OrderBook::getImbalance(size_t k) const {
  uint64_t total_bid_qty = 0;
  uint64_t total_ask_qty = 0;

  PriceLevel* bid_level = bids_;
  for (size_t i = 0; i < k && bid_level; i++) {
    total_bid_qty += bid_level->total_qty;
    bid_level = bid_level->next;
    if (bid_level == bids_) break;
  }

  PriceLevel* ask_level = asks_;
  for (size_t i = 0; i < k && ask_level; i++) {
    total_ask_qty += ask_level->total_qty;
    ask_level = ask_level->next;
    if (ask_level == asks_) break;
  }

  if (total_bid_qty + total_ask_qty == 0) return 0.0;

  return static_cast<double>(total_bid_qty - total_ask_qty) /
         static_cast<double>(total_bid_qty + total_ask_qty);
}

// Calculate book pressure for K levels
double OrderBook::getBookPressure(size_t k) const {
  double mid = getMidPrice();
  if (mid <= 0.0) return 0.0;

  double bid_pressure = 0.0;
  double ask_pressure = 0.0;

  PriceLevel* bid_level = bids_;
  for (size_t i = 0; i < k && bid_level; i++) {
    double distance = mid - static_cast<double>(bid_level->price);
    if (distance > 0.0) {
      bid_pressure += static_cast<double>(bid_level->total_qty) / distance;
    }
    bid_level = bid_level->next;
    if (bid_level == bids_) break;
  }

  PriceLevel* ask_level = asks_;
  for (size_t i = 0; i < k && ask_level; i++) {
    double distance = static_cast<double>(ask_level->price) - mid;
    if (distance > 0.0) {
      ask_pressure += static_cast<double>(ask_level->total_qty) / distance;
    }
    ask_level = ask_level->next;
    if (ask_level == asks_) break;
  }

  double total = bid_pressure + ask_pressure;
  if (total == 0.0) return 0.0;

  return (bid_pressure - ask_pressure) / total;
}

// Get order rank (1-based)
size_t OrderBook::getOrderRank(uint64_t order_id) const {
  size_t map_index = order_id % MAX_ORDERS;
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
    return 0;
  }

  size_t rank = 1;
  Order* current = order->prev;
  while (current != order) {
    rank++;
    current = current->prev;
  }

  return rank;
}

// Get quantity ahead in queue
uint32_t OrderBook::getQtyAhead(uint64_t order_id) const {
  size_t map_index = order_id % MAX_ORDERS;
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
    return 0;
  }

  uint32_t qty_ahead = 0;
  Order* current = order->prev;
  while (current != order) {
    qty_ahead += current->qty;
    current = current->prev;
  }

  return qty_ahead;
}

// SlidingWindowStats implementation
SlidingWindowStats::SlidingWindowStats() {
  sec_index_.fill(SIZE_MAX);
  sec_count_.fill(0);
}

void SlidingWindowStats::recordTrade(uint64_t timestamp, int32_t price, uint64_t qty) {
  // Calculate amount
  uint64_t amount = static_cast<uint64_t>(price) * qty;

  // Store in ring buffer
  size_t idx = head_;
  timestamps_[idx] = timestamp;
  prices_[idx] = price;
  quantities_[idx] = qty;
  amounts_[idx] = amount;

  // Update incremental statistics
  sum_qty_ += qty;
  sum_amount_ += amount;
  min_price_ = std::min(min_price_, price);
  max_price_ = std::max(max_price_, price);

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

void SlidingWindowStats::evictExpired(uint64_t current_timestamp) {
  uint64_t cutoff_time = current_timestamp - WINDOW_SECONDS;

  // Use secondary index for efficient eviction
  while (count_ > 0) {
    size_t tail_idx = (head_ + (MAX_TRADES - count_)) % MAX_TRADES;
    uint64_t tail_time = timestamps_[tail_idx];

    if (tail_time > cutoff_time) {
      break;  // All remaining trades are within window
    }

    // Evict this trade
    uint64_t qty = quantities_[tail_idx];
    uint64_t amount = amounts_[tail_idx];
    int32_t price = prices_[tail_idx];

    sum_qty_ -= qty;
    sum_amount_ -= amount;

    // Note: min/max may need lazy recalculation, but for HFT it's often acceptable
    // to approximate or recalculate periodically. For now, we accept that min/max
    // may be slightly stale after evicting the extreme value.

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

  // If we evicted all trades, reset min/max
  if (count_ == 0) {
    min_price_ = INT32_MAX;
    max_price_ = INT32_MIN;
  }
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

  // Find which price level the VWAP falls into
  int32_t vwap_price = static_cast<int32_t>(vwap);

  // Check bid levels (从最优到最差遍历)
  size_t bid_levels = ob->getBidLevels();
  for (size_t i = 0; i < bid_levels; i++) {
    int32_t price = ob->getBidPrice(i);
    if (vwap_price >= price) {
      return static_cast<int32_t>(i);
    }
  }

  // Check ask levels
  size_t ask_levels = ob->getAskLevels();
  for (size_t i = 0; i < ask_levels; i++) {
    int32_t price = ob->getAskPrice(i);
    if (vwap_price <= price) {
      return static_cast<int32_t>(-static_cast<int32_t>(i));
    }
  }

  return 0;
}

// OrderBook time window metrics implementation
void OrderBook::evictExpiredTrades(uint64_t current_timestamp) {
  window_stats_.evictExpired(current_timestamp);
}

int32_t OrderBook::getPriceRange() const {
  return window_stats_.getPriceRange();
}

uint64_t OrderBook::getWindowVolume() const {
  return window_stats_.getTotalVolume();
}

uint64_t OrderBook::getWindowAmount() const {
  return window_stats_.getTotalAmount();
}

uint64_t OrderBook::getVWAP() const {
  return window_stats_.getVWAP();
}

int32_t OrderBook::getMedianPrice() const {
  return window_stats_.getMedianPrice();
}

int32_t OrderBook::getVWAPLevel() const {
  return window_stats_.getVWAPLevel(this);
}

} // namespace impl
