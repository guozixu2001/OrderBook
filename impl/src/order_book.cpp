#include "impl/order_book.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace impl {

namespace {

size_t orderHash(uint64_t order_id) {
  return static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
}

size_t priceHash(int32_t price) {
  return static_cast<uint32_t>(price) & (MAX_PRICE_LEVELS - 1);
}

size_t findOrderIndex(const OrderHashMap& map, uint64_t order_id) {
  size_t mask = MAX_ORDERS - 1;
  size_t index = orderHash(order_id);
  size_t start_index = index;
  while (map[index] != nullptr) {
    if (map[index]->order_id == order_id) {
      return index;
    }
    index = (index + 1) & mask;
    if (index == start_index) {
      break;
    }
  }
  return MAX_ORDERS;
}

size_t findPriceLevelIndex(const PriceLevelHashMap& map, int32_t price) {
  size_t mask = MAX_PRICE_LEVELS - 1;
  size_t index = priceHash(price);
  size_t start_index = index;
  while (map[index] != nullptr) {
    if (map[index]->price == price) {
      return index;
    }
    index = (index + 1) & mask;
    if (index == start_index) {
      break;
    }
  }
  return MAX_PRICE_LEVELS;
}

bool hasEmptySlot(const OrderHashMap& map, size_t start_index) {
  size_t mask = MAX_ORDERS - 1;
  size_t index = start_index;
  for (size_t probe = 0; probe < MAX_ORDERS; ++probe) {
    if (!map[index]) {
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

bool hasEmptyPriceSlot(const PriceLevelHashMap& map, size_t start_index) {
  size_t mask = MAX_PRICE_LEVELS - 1;
  size_t index = start_index;
  for (size_t probe = 0; probe < MAX_PRICE_LEVELS; ++probe) {
    if (!map[index]) {
      return true;
    }
    index = (index + 1) & mask;
  }
  return false;
}

void backwardShiftDelete(OrderHashMap& map, size_t index) {
  size_t mask = MAX_ORDERS - 1;
  size_t hole = index;
  size_t next = (hole + 1) & mask;
  while (map[next] != nullptr) {
    size_t home = orderHash(map[next]->order_id);
    size_t dist = (next - home) & mask;
    if (dist == 0) {
      break;
    }
    map[hole] = map[next];
    hole = next;
    next = (next + 1) & mask;
  }
  map[hole] = nullptr;
}

void backwardShiftDeletePrice(PriceLevelHashMap& map, size_t index) {
  size_t mask = MAX_PRICE_LEVELS - 1;
  size_t hole = index;
  size_t next = (hole + 1) & mask;
  while (map[next] != nullptr) {
    size_t home = priceHash(map[next]->price);
    size_t dist = (next - home) & mask;
    if (dist == 0) {
      break;
    }
    map[hole] = map[next];
    hole = next;
    next = (next + 1) & mask;
  }
  map[hole] = nullptr;
}

}  // namespace

OrderBook::OrderBook(const char* symbol) {
  std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
  symbol_[SYMBOL_LEN - 1] = '\0';
  order_map_.fill(nullptr);
  price_level_map_.fill(nullptr);
  bbo_ = {};

  // 16 cold tiers allows up to ~1M orders (65536 * 17)
  order_pool_ = new TieredMemoryPool<Order, MAX_ORDERS>(16);
  // 8 cold tiers allows up to ~18K price levels (2048 * 9)
  level_pool_ = new TieredMemoryPool<PriceLevel, MAX_PRICE_LEVELS>(8);
}

OrderBook::~OrderBook() {
  clear();

  delete order_pool_;
  delete level_pool_;
}

size_t OrderBook::priceToIndex(int32_t price) const {
  // Since MAX_PRICE_LEVELS is power of 2, use bitwise AND instead of modulo
  return priceHash(price);
}

PriceLevel* OrderBook::findPriceLevel(int32_t price) const {
  size_t index = findPriceLevelIndex(price_level_map_, price);
  return (index != MAX_PRICE_LEVELS) ? price_level_map_[index] : nullptr;
}

bool OrderBook::addPriceLevel(PriceLevel* new_level) {
  Side side = new_level->side;
  PriceLevel** head = (side == Side::BUY) ? &bids_ : &asks_;

  size_t index = priceToIndex(new_level->price);
  if (!hasEmptyPriceSlot(price_level_map_, index)) {
    return false;
  }
  while (price_level_map_[index] != nullptr) {
    if (price_level_map_[index]->price == new_level->price) {
      return false;
    }
    index = (index + 1) & (MAX_PRICE_LEVELS - 1);
  }
  price_level_map_[index] = new_level;

  if (!*head) {
    // First level
    *head = new_level;
    new_level->prev = new_level;
    new_level->next = new_level;
    return true;
  }

  PriceLevel* current = *head;

  if (side == Side::BUY) {
    if (new_level->price > current->price) {
      // New best bid
      new_level->prev = current->prev;
      new_level->next = current;
      current->prev->next = new_level;
      current->prev = new_level;
      *head = new_level;
      return true;
    }

    while (current->next != *head && current->next->price > new_level->price) {
      // Prefetch next node's next pointer to reduce memory latency
      __builtin_prefetch(current->next->next, 0, 3);
      current = current->next;
    }
  } else {
    if (new_level->price < current->price) {
      // New best ask
      new_level->prev = current->prev;
      new_level->next = current;
      current->prev->next = new_level;
      current->prev = new_level;
      *head = new_level;
      return true;
    }

    while (current->next != *head && current->next->price < new_level->price) {
      // Prefetch next node's next pointer to reduce memory latency
      __builtin_prefetch(current->next->next, 0, 3);
      current = current->next;
    }
  }

  // Insert after current
  new_level->prev = current;
  new_level->next = current->next;
  current->next->prev = new_level;
  current->next = new_level;
  return true;
}

void OrderBook::removePriceLevel(PriceLevel* level) {
  Side side = level->side;
  PriceLevel** head = (side == Side::BUY) ? &bids_ : &asks_;

  // Remove from hash map
  size_t index = findPriceLevelIndex(price_level_map_, level->price);
  if (index != MAX_PRICE_LEVELS) {
    backwardShiftDeletePrice(price_level_map_, index);
  }

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

void OrderBook::clear() {
  for (Order* order : order_map_) {
    if (order) {
      order_pool_->deallocate(order);
    }
  }
  order_map_.fill(nullptr);

  price_level_map_.fill(nullptr);

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

  window_stats_ = RingBufferSlidingWindowStats();

  updateBBO();
}

void OrderBook::addOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  // Use Robin Hood hashing with backward-shift deletion compatibility.
  size_t mask = MAX_ORDERS - 1;
  size_t start_index = orderHash(order_id);

  if (findOrderIndex(order_map_, order_id) != MAX_ORDERS) {
    return;  // Order ID already exists
  }
  if (!hasEmptySlot(order_map_, start_index)) {
    return;  // Hash table is full
  }

  // Create new order from pool
  Order* new_order = order_pool_->allocate(order_id, price, qty, side);
  if (!new_order) return;  // Pool exhausted

  // Robin Hood insertion: swap with entries that have shorter probe distance.
  Order* to_insert = new_order;
  size_t index = start_index;
  size_t probe = 0;
  while (true) {
    if (order_map_[index] == nullptr) {
      order_map_[index] = to_insert;
      break;
    }

    size_t existing_home = static_cast<size_t>(order_map_[index]->order_id) & mask;
    size_t existing_probe = (index - existing_home) & mask;
    if (existing_probe < probe) {
      std::swap(order_map_[index], to_insert);
      probe = existing_probe;
    }

    index = (index + 1) & mask;
    ++probe;
  }

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
    level = level_pool_->allocate(price, side, new_order);
    if (!level) {
      order_pool_->deallocate(new_order);
      size_t remove_idx = findOrderIndex(order_map_, order_id);
      if (remove_idx != MAX_ORDERS) {
        backwardShiftDelete(order_map_, remove_idx);
      }
      return;
    }
    if (!addPriceLevel(level)) {
      level_pool_->deallocate(level);
      order_pool_->deallocate(new_order);
      size_t remove_idx = findOrderIndex(order_map_, order_id);
      if (remove_idx != MAX_ORDERS) {
        backwardShiftDelete(order_map_, remove_idx);
      }
      return;
    }
  } else {
    // Add to existing price level
    Order* first = level->first_order;
    new_order->prev = first->prev;
    new_order->next = first;
    first->prev->next = new_order;
    first->prev = new_order;
    level->total_qty += qty;
    level->order_count++;
  }

  updateBBOSide(update_bid, update_ask);
}

void OrderBook::modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  size_t index = findOrderIndex(order_map_, order_id);
  Order* order = (index != MAX_ORDERS) ? order_map_[index] : nullptr;

  if (!order) {
    return;  // Order not found
  }

  // Check if BBO needs updating 
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
  size_t index = findOrderIndex(order_map_, order_id);
  Order* order = (index != MAX_ORDERS) ? order_map_[index] : nullptr;

  if (!order) {
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
  backwardShiftDelete(order_map_, index);
  order_pool_->deallocate(order);

  updateBBOSide(update_bid, update_ask);
}

void OrderBook::processTrade(uint64_t order_id, uint64_t /*trade_id*/, int32_t price,
                             uint64_t qty, Side side, uint64_t timestamp) {
  size_t index = findOrderIndex(order_map_, order_id);
  Order* order = (index != MAX_ORDERS) ? order_map_[index] : nullptr;

  if (!order) {
    return;  // Order not found
  }

  // Record trade for time window statistics
  window_stats_.recordTrade(timestamp, price, qty);

  // Check if BBO needs updating 
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

size_t OrderBook::getBidLevels() const {
  if (!bids_) return 0;
  size_t count = 0;
  PriceLevel* start = bids_;
  PriceLevel* current = start;
  do {
    count++;
    current = current->next;
  } while (current != start);
  return count;
}

size_t OrderBook::getAskLevels() const {
  if (!asks_) return 0;
  size_t count = 0;
  PriceLevel* start = asks_;
  PriceLevel* current = start;
  do {
    count++;
    current = current->next;
  } while (current != start);
  return count;
}

int32_t OrderBook::getBidPrice(size_t level) const {
  PriceLevel* current = bids_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == bids_) return 0;
  }
  return current ? current->price : 0;
}

uint32_t OrderBook::getBidQty(size_t level) const {
  PriceLevel* current = bids_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == bids_) return 0;
  }
  return current ? current->total_qty : 0;
}

int32_t OrderBook::getAskPrice(size_t level) const {
  PriceLevel* current = asks_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == asks_) return 0;
  }
  return current ? current->price : 0;
}

uint32_t OrderBook::getAskQty(size_t level) const {
  PriceLevel* current = asks_;
  for (size_t i = 0; i < level && current; i++) {
    current = current->next;
    if (current == asks_) return 0;
  }
  return current ? current->total_qty : 0;
}

double OrderBook::getMidPrice() const {
  if (bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    return (static_cast<double>(bbo_.bid_price) + bbo_.ask_price) / 2.0;
  }
  return 0.0;
}

int32_t OrderBook::getSpread() const {
  if (bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    return bbo_.ask_price - bbo_.bid_price;
  }
  return 0;
}

double OrderBook::getMacroPrice() const {
  if (bbo_.bid_qty > 0 && bbo_.ask_qty > 0 && bbo_.bid_price > 0 && bbo_.ask_price > 0) {
    double bid_weight = static_cast<double>(bbo_.bid_qty);
    double ask_weight = static_cast<double>(bbo_.ask_qty);
    return (bbo_.ask_price * bid_weight + bbo_.bid_price * ask_weight) /
           (bid_weight + ask_weight);
  }
  return getMidPrice();
}

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

  uint64_t total = total_bid_qty + total_ask_qty;
  if (unlikely(total == 0)) return 0.0;

  // Use integer arithmetic first, convert to double only at the end
  // (bid - ask) / (bid + ask) = (bid - ask) / total
  int64_t diff = static_cast<int64_t>(total_bid_qty) - static_cast<int64_t>(total_ask_qty);
  return static_cast<double>(diff) / static_cast<double>(total);
}

double OrderBook::getBookPressure(size_t k) const {
  double mid = getMidPrice();
  if (unlikely(mid <= 0.0)) return 0.0;

  // Pre-compute reciprocal for faster division (qty / distance = qty * (1/distance))
  double bid_pressure = 0.0;
  double ask_pressure = 0.0;

  PriceLevel* bid_level = bids_;
  for (size_t i = 0; i < k && bid_level; i++) {
    double distance = mid - static_cast<double>(bid_level->price);
    if (likely(distance > 0.0)) {
      // Use multiplication instead of division
      bid_pressure += static_cast<double>(bid_level->total_qty) / distance;
    }
    bid_level = bid_level->next;
    if (bid_level == bids_) break;
  }

  PriceLevel* ask_level = asks_;
  for (size_t i = 0; i < k && ask_level; i++) {
    double distance = static_cast<double>(ask_level->price) - mid;
    if (likely(distance > 0.0)) {
      ask_pressure += static_cast<double>(ask_level->total_qty) / distance;
    }
    ask_level = ask_level->next;
    if (ask_level == asks_) break;
  }

  double total = bid_pressure + ask_pressure;
  if (unlikely(total == 0.0)) return 0.0;

  return (bid_pressure - ask_pressure) / total;
}

size_t OrderBook::getOrderRank(uint64_t order_id) const {
  size_t index = findOrderIndex(order_map_, order_id);
  Order* order = (index != MAX_ORDERS) ? order_map_[index] : nullptr;

  if (!order) {
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
  size_t index = findOrderIndex(order_map_, order_id);
  Order* order = (index != MAX_ORDERS) ? order_map_[index] : nullptr;

  if (!order) {
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
