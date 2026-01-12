#include "impl/order_book.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace impl {

OrderBook::OrderBook(const char* symbol) {
  std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
  symbol_[SYMBOL_LEN - 1] = '\0';
  order_map_.fill(nullptr);
  price_level_map_.fill(nullptr);
  bbo_ = {};

  order_pool_ = new MemoryPool<Order, MAX_ORDERS>();
  level_pool_ = new MemoryPool<PriceLevel, MAX_PRICE_LEVELS>();
}

OrderBook::~OrderBook() {
  clear();

  delete order_pool_;
  delete level_pool_;
}

size_t OrderBook::priceToIndex(int32_t price) const {
  uint32_t abs_price = static_cast<uint32_t>(price);
  // Since MAX_PRICE_LEVELS is power of 2, use bitwise AND instead of modulo
  return abs_price & (MAX_PRICE_LEVELS - 1);
}

PriceLevel* OrderBook::findPriceLevel(int32_t price) const {
  size_t index = priceToIndex(price);
  PriceLevel* level = price_level_map_[index];

  // Verify the level matches the price (handle hash collisions)
  if (level && level->price == price) {
    return level;
  }
  return nullptr;
}

void OrderBook::addPriceLevel(PriceLevel* new_level) {
  Side side = new_level->side;
  PriceLevel** head = (side == Side::BUY) ? &bids_ : &asks_;

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
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
  if (order_map_[map_index] != nullptr) {
    // Order ID already exists
    return;
  }

  // Create new order from pool
  Order* order = order_pool_->allocate(order_id, price, qty, side);
  if (!order) return;  // Pool exhausted

  order_map_[map_index] = order;

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

void OrderBook::modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
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
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
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

void OrderBook::processTrade(uint64_t order_id, uint64_t /*trade_id*/, int32_t price,
                             uint64_t qty, Side side, uint64_t timestamp) {
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
  Order* order = order_map_[map_index];

  if (!order || order->order_id != order_id) {
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
  size_t count = 0;
  PriceLevel* current = bids_;
  while (current) {
    count++;
    if (current->next == bids_) break;
    current = current->next;
  }
  return count;
}

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

  if (total_bid_qty + total_ask_qty == 0) return 0.0;

  return (static_cast<double>(total_bid_qty) - static_cast<double>(total_ask_qty)) /
         static_cast<double>(total_bid_qty + total_ask_qty);
}

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

size_t OrderBook::getOrderRank(uint64_t order_id) const {
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
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
  // Since MAX_ORDERS is power of 2, use bitwise AND instead of modulo
  size_t map_index = static_cast<size_t>(order_id) & (MAX_ORDERS - 1);
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
