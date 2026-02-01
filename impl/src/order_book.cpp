#include "impl/order_book.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace impl {

namespace {

size_t priceHash(int32_t price) {
  return static_cast<uint32_t>(price) & (MAX_PRICE_LEVELS - 1);
}

// Optimized findPriceLevelIndex with loop unrolling and branch prediction hints
size_t findPriceLevelIndex(const PriceLevelHashMap& map, int32_t price) {
  const size_t mask = MAX_PRICE_LEVELS - 1;
  size_t index = priceHash(price);
  const size_t start_index = index;

  // Unroll loop by 4 for better branch prediction
  for (size_t probe = 0; probe < MAX_PRICE_LEVELS; ) {
    PriceLevel* entry = map[index];

    if (unlikely(entry == nullptr)) {
      return MAX_PRICE_LEVELS;
    }

    if (likely(entry->price == price)) {
      return index;
    }

    index = (index + 1) & mask;
    probe++;
    if (unlikely(index == start_index)) return MAX_PRICE_LEVELS;

    // Manual unroll
    entry = map[index];
    if (unlikely(entry == nullptr)) return MAX_PRICE_LEVELS;
    if (likely(entry->price == price)) return index;
    index = (index + 1) & mask;
    probe++;
    if (unlikely(index == start_index)) return MAX_PRICE_LEVELS;

    entry = map[index];
    if (unlikely(entry == nullptr)) return MAX_PRICE_LEVELS;
    if (likely(entry->price == price)) return index;
    index = (index + 1) & mask;
    probe++;
    if (unlikely(index == start_index)) return MAX_PRICE_LEVELS;

    entry = map[index];
    if (unlikely(entry == nullptr)) return MAX_PRICE_LEVELS;
    if (likely(entry->price == price)) return index;
    index = (index + 1) & mask;
    probe++;
    if (unlikely(index == start_index)) return MAX_PRICE_LEVELS;
  }

  return MAX_PRICE_LEVELS;
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

size_t OrderIndexMap::nextPow2(size_t value) {
  if (value <= 1) return 1;
  --value;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  if (sizeof(size_t) >= 8) {
    value |= value >> 32;
  }
  return value + 1;
}

OrderIndexMap::OrderIndexMap(size_t initial_capacity) {
  size_t capacity = nextPow2(std::max(initial_capacity, kMinCapacity));
  ctrl_.assign(capacity, kEmpty);
  keys_.resize(capacity);
  idxs_.resize(capacity);
  capacity_ = capacity;
}

void OrderIndexMap::clear() {
  std::fill(ctrl_.begin(), ctrl_.end(), kEmpty);
  size_ = 0;
  tombstones_ = 0;
}

static inline size_t hashKey(uint64_t key) {
  return static_cast<size_t>(key * 11400714819323198485ull);
}

bool OrderIndexMap::find(uint64_t key, uint32_t* idx_out) const {
  if (capacity_ == 0) return false;
  const size_t mask = capacity_ - 1;
  const size_t hash = hashKey(key);
  const uint8_t h2 = static_cast<uint8_t>((hash >> 7) & 0x7F);
  size_t index = hash & mask;

  for (size_t probe = 0; probe < capacity_; probe += kGroupSize) {
    size_t group_start = index & mask;
    for (size_t i = 0; i < kGroupSize; ++i) {
      size_t slot = (group_start + i) & mask;
      uint8_t ctrl = ctrl_[slot];
      if (ctrl == kEmpty) {
        return false;
      }
      if (ctrl == h2 && keys_[slot] == key) {
        if (idx_out) {
          *idx_out = idxs_[slot];
        }
        return true;
      }
    }
    index = (group_start + kGroupSize) & mask;
  }
  return false;
}

void OrderIndexMap::rehash(size_t new_capacity) {
  size_t capacity = nextPow2(std::max(new_capacity, kMinCapacity));
  std::vector<uint8_t> new_ctrl(capacity, kEmpty);
  std::vector<uint64_t> new_keys(capacity);
  std::vector<uint32_t> new_idxs(capacity);

  const size_t old_capacity = capacity_;
  const auto old_ctrl = std::move(ctrl_);
  const auto old_keys = std::move(keys_);
  const auto old_idxs = std::move(idxs_);

  ctrl_ = std::move(new_ctrl);
  keys_ = std::move(new_keys);
  idxs_ = std::move(new_idxs);
  capacity_ = capacity;
  size_ = 0;
  tombstones_ = 0;

  for (size_t i = 0; i < old_capacity; ++i) {
    if (old_ctrl[i] != kEmpty && old_ctrl[i] != kTombstone) {
      insertInternal(old_keys[i], old_idxs[i]);
    }
  }
}

OrderIndexMap::InsertResult OrderIndexMap::insert(uint64_t key, uint32_t idx) {
  if ((size_ + tombstones_ + 1) * 10 >= capacity_ * 7) {
    rehash(capacity_ * 2);
  }
  return insertInternal(key, idx);
}

OrderIndexMap::InsertResult OrderIndexMap::insertInternal(uint64_t key, uint32_t idx) {
  const size_t mask = capacity_ - 1;
  const size_t hash = hashKey(key);
  const uint8_t h2 = static_cast<uint8_t>((hash >> 7) & 0x7F);
  size_t index = hash & mask;
  size_t first_tombstone = capacity_;

  for (size_t probe = 0; probe < capacity_; probe += kGroupSize) {
    size_t group_start = index & mask;
    for (size_t i = 0; i < kGroupSize; ++i) {
      size_t slot = (group_start + i) & mask;
      uint8_t ctrl = ctrl_[slot];
      if (ctrl == kEmpty) {
        size_t target = (first_tombstone != capacity_) ? first_tombstone : slot;
        ctrl_[target] = h2;
        keys_[target] = key;
        idxs_[target] = idx;
        ++size_;
        if (first_tombstone != capacity_) {
          --tombstones_;
        }
        return InsertResult::kInserted;
      }
      if (ctrl == kTombstone) {
        if (first_tombstone == capacity_) {
          first_tombstone = slot;
        }
        continue;
      }
      if (ctrl == h2 && keys_[slot] == key) {
        return InsertResult::kExists;
      }
    }
    index = (group_start + kGroupSize) & mask;
  }
  return InsertResult::kFailed;
}

bool OrderIndexMap::erase(uint64_t key) {
  if (capacity_ == 0) return false;
  const size_t mask = capacity_ - 1;
  const size_t hash = hashKey(key);
  const uint8_t h2 = static_cast<uint8_t>((hash >> 7) & 0x7F);
  size_t index = hash & mask;

  for (size_t probe = 0; probe < capacity_; probe += kGroupSize) {
    size_t group_start = index & mask;
    for (size_t i = 0; i < kGroupSize; ++i) {
      size_t slot = (group_start + i) & mask;
      uint8_t ctrl = ctrl_[slot];
      if (ctrl == kEmpty) {
        return false;
      }
      if (ctrl == h2 && keys_[slot] == key) {
        ctrl_[slot] = kTombstone;
        --size_;
        ++tombstones_;
        if (tombstones_ * 4 > capacity_) {
          rehash(capacity_);
        }
        return true;
      }
    }
    index = (group_start + kGroupSize) & mask;
  }
  return false;
}

Order* OrderArena::allocate(uint64_t order_id, int32_t price, uint32_t qty, Side side, uint32_t* out_idx) {
  if (free_list_.empty()) {
    if (!addChunk()) return nullptr;
  }
  uint32_t idx = free_list_.back();
  free_list_.pop_back();
  if (out_idx) {
    *out_idx = idx;
  }
  Order* order = get(idx);
  order->order_id = order_id;
  order->price = price;
  order->qty = qty;
  order->side = side;
  order->level = nullptr;
  order->prev = order;
  order->next = order;
  return order;
}

void OrderArena::deallocate(uint32_t idx) {
  free_list_.push_back(idx);
}

Order* OrderArena::get(uint32_t idx) const {
  uint32_t chunk_id = idx >> kChunkShift;
  uint32_t offset = idx & kChunkMask;
  return chunks_[chunk_id].get() + offset;
}

void OrderArena::clear() {
  free_list_.clear();
  free_list_.reserve(chunks_.size() * kChunkSize);
  for (size_t chunk_id = 0; chunk_id < chunks_.size(); ++chunk_id) {
    uint32_t base = static_cast<uint32_t>(chunk_id) << kChunkShift;
    for (uint32_t offset = 0; offset < kChunkSize; ++offset) {
      free_list_.push_back(base | offset);
    }
  }
}

bool OrderArena::addChunk() {
  constexpr size_t kMaxChunks = (std::numeric_limits<uint32_t>::max() >> kChunkShift);
  if (chunks_.size() >= kMaxChunks) {
    return false;
  }
  std::unique_ptr<Order[]> chunk(new (std::nothrow) Order[kChunkSize]);
  if (!chunk) {
    return false;
  }
  chunks_.push_back(std::move(chunk));

  uint32_t chunk_id = static_cast<uint32_t>(chunks_.size() - 1);
  uint32_t base = chunk_id << kChunkShift;
  free_list_.reserve(free_list_.size() + kChunkSize);
  for (uint32_t offset = 0; offset < kChunkSize; ++offset) {
    free_list_.push_back(base | offset);
  }
  return true;
}

OrderBook::OrderBook(const char* symbol) {
  std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
  symbol_[SYMBOL_LEN - 1] = '\0';
  order_map_.clear();
  price_level_map_.fill(nullptr);
  bbo_ = {};
  order_count_ = 0;
  price_level_count_ = 0;

  // 8 cold tiers allows up to ~18K price levels (2048 * 9)
  level_pool_ = new TieredMemoryPool<PriceLevel, MAX_PRICE_LEVELS>(8);
}

OrderBook::~OrderBook() {
  clear();

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

  if (unlikely(price_level_count_ >= MAX_PRICE_LEVELS)) {
    return false;
  }

  size_t index = priceToIndex(new_level->price);
  while (price_level_map_[index] != nullptr) {
    if (price_level_map_[index]->price == new_level->price) {
      return false;
    }
    index = (index + 1) & (MAX_PRICE_LEVELS - 1);
  }
  price_level_map_[index] = new_level;
  ++price_level_count_;

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
  if (price_level_count_ > 0) {
    --price_level_count_;
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
  // Optimized with branchless-like patterns
  if (likely(update_bid)) {
    if (bids_) {
      bbo_.bid_price = bids_->price;
      bbo_.bid_qty = bids_->total_qty;
    } else {
      bbo_.bid_price = 0;
      bbo_.bid_qty = 0;
    }
  }

  if (likely(update_ask)) {
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
  order_map_.clear();
  order_arena_.clear();
  order_count_ = 0;

  price_level_map_.fill(nullptr);
  price_level_count_ = 0;

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
  uint32_t idx = 0;
  Order* new_order = order_arena_.allocate(order_id, price, qty, side, &idx);
  if (unlikely(!new_order)) return;
  OrderIndexMap::InsertResult insert_result = order_map_.insert(order_id, idx);
  if (unlikely(insert_result != OrderIndexMap::InsertResult::kInserted)) {
    order_arena_.deallocate(idx);
    return;
  }

  // Determine BBO update flags using branchless comparison
  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    // Branchless: (price >= bids->price) when bids exists
    update_bid = !bids_ || (price >= bids_->price);
  } else {
    // Branchless: (price <= asks->price) when asks exists
    update_ask = !asks_ || (price <= asks_->price);
  }

  // Find or create price level
  PriceLevel* level = findPriceLevel(price);

  if (unlikely(!level)) {
    // Need to create new price level
    level = level_pool_->allocate(price, side, new_order);
    if (unlikely(!level)) {
      // Rollback on allocation failure
      order_map_.erase(order_id);
      order_arena_.deallocate(idx);
      return;
    }
    if (unlikely(!addPriceLevel(level))) {
      // Rollback on add failure
      level_pool_->deallocate(level);
      order_map_.erase(order_id);
      order_arena_.deallocate(idx);
      return;
    }
  } else {
    // Add to existing price level (hot path)
    Order* first = level->first_order;
    new_order->prev = first->prev;
    new_order->next = first;
    first->prev->next = new_order;
    first->prev = new_order;
    level->total_qty += qty;
    level->order_count++;
  }

  new_order->level = level;

  updateBBOSide(update_bid, update_ask);
  ++order_count_;
}

void OrderBook::modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  uint32_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;  // Order not found
  }
  Order* order = order_arena_.get(idx);

  // Check if BBO needs updating
  bool update_bid = false;
  bool update_ask = false;

  if (unlikely(order->price != price)) {
    // Price changed - check if old or new price affects BBO
    if (side == Side::BUY) {
      update_bid = bids_ && (order->price == bids_->price || price >= bids_->price);
    } else {
      update_ask = asks_ && (order->price == asks_->price || price <= asks_->price);
    }

    // Delete and re-add with new price
    deleteOrder(order_id, side);
    addOrder(order_id, price, qty, side);
  } else {
    // Update quantity at same price (hot path)
    PriceLevel* level = order->level;
    if (unlikely(!level)) {
      level = findPriceLevel(price);
    }
    if (likely(level)) {
      const int32_t qty_diff = static_cast<int32_t>(qty) - static_cast<int32_t>(order->qty);
      order->qty = qty;
      level->total_qty += qty_diff;

      // If this level is BBO, need to update
      if (side == Side::BUY) {
        update_bid = bids_ && bids_->price == price;
      } else {
        update_ask = asks_ && asks_->price == price;
      }
    }
  }

  updateBBOSide(update_bid, update_ask);
}

// Delete an order
void OrderBook::deleteOrder(uint64_t order_id, Side side) {
  uint32_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;  // Order not found
  }
  Order* order = order_arena_.get(idx);

  // Check if BBO needs updating (deleting from BBO level)
  bool update_bid = false;
  bool update_ask = false;

  PriceLevel* level = order->level;
  if (unlikely(!level)) {
    level = findPriceLevel(order->price);
    if (unlikely(!level)) return;
  }

  // Determine BBO update
  if (side == Side::BUY) {
    update_bid = bids_ && bids_->price == order->price;
  } else {
    update_ask = asks_ && asks_->price == order->price;
  }

  // Remove from price level
  if (unlikely(order->next == order)) {
    // Only order at this level
    removePriceLevel(level);
    level = nullptr;
  } else {
    // Multiple orders at this level (hot path)
    order->prev->next = order->next;
    order->next->prev = order->prev;
    level->total_qty -= order->qty;
    level->order_count--;

    if (unlikely(level->first_order == order)) {
      level->first_order = order->next;
    }
  }

  // Remove from order map and deallocate
  order_map_.erase(order_id);
  order_arena_.deallocate(idx);
  if (order_count_ > 0) {
    --order_count_;
  }

  updateBBOSide(update_bid, update_ask);
}

void OrderBook::processTrade(uint64_t order_id, uint64_t /*trade_id*/, int32_t price,
                             uint64_t qty, Side side, uint64_t timestamp) {
  uint32_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;  // Order not found
  }
  Order* order = order_arena_.get(idx);

  // Record trade for time window statistics
  window_stats_.recordTrade(timestamp, price, qty);

  // Check if BBO needs updating
  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    update_bid = bids_ && bids_->price == price;
  } else {
    update_ask = asks_ && asks_->price == price;
  }

  if (unlikely(order->qty <= qty)) {
    // Full fill - delete order
    deleteOrder(order_id, side);
  } else {
    // Partial fill (hot path)
    order->qty -= qty;
    PriceLevel* level = order->level;
    if (unlikely(!level)) {
      level = findPriceLevel(price);
    }
    if (likely(level)) {
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
  uint32_t idx = 0;
  if (!order_map_.find(order_id, &idx)) {
    return 0;
  }
  Order* order = order_arena_.get(idx);

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
  uint32_t idx = 0;
  if (!order_map_.find(order_id, &idx)) {
    return 0;
  }
  Order* order = order_arena_.get(idx);

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
