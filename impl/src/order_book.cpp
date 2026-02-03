#include "impl/order_book.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace impl {

namespace {

static inline size_t hashKey(uint64_t key) {
  return static_cast<size_t>(key * 11400714819323198485ull);
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

bool OrderIndexMap::find(uint64_t key, size_t* idx_out) const {
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
  std::vector<size_t> new_idxs(capacity);

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

OrderIndexMap::InsertResult OrderIndexMap::insert(uint64_t key, size_t idx) {
  if ((size_ + tombstones_ + 1) * 10 >= capacity_ * 7) {
    rehash(capacity_ * 2);
  }
  return insertInternal(key, idx);
}

OrderIndexMap::InsertResult OrderIndexMap::insertInternal(uint64_t key, size_t idx) {
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

Order* OrderArena::allocate(uint64_t order_id, int32_t price, uint32_t qty, Side side, size_t* out_idx) {
  if (free_list_.empty()) {
    if (!addChunk()) return nullptr;
  }
  size_t idx = free_list_.back();
  free_list_.pop_back();
  if (out_idx) {
    *out_idx = idx;
  }
  Order* order = get(idx);
  order->order_id = order_id;
  order->price = price;
  order->qty = qty;
  order->side = side;
  order->level_id = INVALID_INDEX;
  order->prev_idx = idx;
  order->next_idx = idx;
  return order;
}

void OrderArena::deallocate(size_t idx) {
  free_list_.push_back(idx);
}

Order* OrderArena::get(size_t idx) const {
  size_t chunk_id = idx >> kChunkShift;
  size_t offset = idx & kChunkMask;
  return chunks_[chunk_id].get() + offset;
}

void OrderArena::clear() {
  free_list_.clear();
  free_list_.reserve(chunks_.size() * kChunkSize);
  for (size_t chunk_id = 0; chunk_id < chunks_.size(); ++chunk_id) {
    size_t base = chunk_id << kChunkShift;
    for (size_t offset = 0; offset < kChunkSize; ++offset) {
      free_list_.push_back(base | offset);
    }
  }
}

bool OrderArena::addChunk() {
  constexpr size_t kMaxChunks = (std::numeric_limits<size_t>::max() >> kChunkShift);
  if (chunks_.size() >= kMaxChunks) {
    return false;
  }
  std::unique_ptr<Order[]> chunk(new (std::nothrow) Order[kChunkSize]);
  if (!chunk) {
    return false;
  }
  chunks_.push_back(std::move(chunk));

  size_t chunk_id = chunks_.size() - 1;
  size_t base = chunk_id << kChunkShift;
  free_list_.reserve(free_list_.size() + kChunkSize);
  for (size_t offset = 0; offset < kChunkSize; ++offset) {
    free_list_.push_back(base | offset);
  }
  return true;
}

PriceLevelStore::LevelId PriceLevelStore::allocate(int32_t price, Side side, size_t order_idx, uint32_t qty) {
  LevelId id = INVALID_INDEX;
  if (!free_list_.empty()) {
    id = free_list_.back();
    free_list_.pop_back();
    if (id >= levels_.size()) {
      levels_.resize(id + 1);
    }
  } else {
    id = levels_.size();
    levels_.push_back(PriceLevel());
  }

  levels_[id] = PriceLevel(price, side, order_idx, qty, id);
  return id;
}

void PriceLevelStore::deallocate(LevelId id) {
  if (id == INVALID_INDEX) return;
  free_list_.push_back(id);
}

PriceLevel* PriceLevelStore::get(LevelId id) {
  if (id >= levels_.size()) return nullptr;
  return &levels_[id];
}

const PriceLevel* PriceLevelStore::get(LevelId id) const {
  if (id >= levels_.size()) return nullptr;
  return &levels_[id];
}

void PriceLevelStore::clear() {
  levels_.clear();
  free_list_.clear();
}

OrderBook::OrderBook(const char* symbol) {
  std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
  symbol_[SYMBOL_LEN - 1] = '\0';
  bbo_ = {};
  clear();
}

OrderBook::~OrderBook() {
  clear();
}

PriceLevel* OrderBook::findPriceLevel(Side side, int32_t price) {
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  bool found = false;
  if (side == Side::BUY) {
    found = bid_tree_.find(price, &level_id);
  } else {
    found = ask_tree_.find(price, &level_id);
  }
  if (!found) return nullptr;
  return level_store_.get(level_id);
}

const PriceLevel* OrderBook::findPriceLevel(Side side, int32_t price) const {
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  bool found = false;
  if (side == Side::BUY) {
    found = bid_tree_.find(price, &level_id);
  } else {
    found = ask_tree_.find(price, &level_id);
  }
  if (!found) return nullptr;
  return level_store_.get(level_id);
}

PriceLevel* OrderBook::addPriceLevel(Side side, int32_t price, size_t order_idx, uint32_t qty) {
  PriceLevelStore::LevelId level_id = level_store_.allocate(price, side, order_idx, qty);
  bool inserted = false;
  if (side == Side::BUY) {
    inserted = bid_tree_.insert(price, level_id);
  } else {
    inserted = ask_tree_.insert(price, level_id);
  }
  if (!inserted) {
    level_store_.deallocate(level_id);
    return nullptr;
  }
  return level_store_.get(level_id);
}

void OrderBook::removePriceLevel(Side side, int32_t price, size_t level_id) {
  if (side == Side::BUY) {
    bid_tree_.erase(price);
  } else {
    ask_tree_.erase(price);
  }
  level_store_.deallocate(level_id);
}

void OrderBook::updateBBO() {
  updateBBOSide(true, true);
}

void OrderBook::updateBBOSide(bool update_bid, bool update_ask) {
  if (likely(update_bid)) {
    int32_t price = 0;
    PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
    if (bid_tree_.max(&price, &level_id)) {
      const PriceLevel* level = level_store_.get(level_id);
      bbo_.bid_price = price;
      bbo_.bid_qty = level ? level->total_qty : 0;
    } else {
      bbo_.bid_price = 0;
      bbo_.bid_qty = 0;
    }
  }

  if (likely(update_ask)) {
    int32_t price = 0;
    PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
    if (ask_tree_.min(&price, &level_id)) {
      const PriceLevel* level = level_store_.get(level_id);
      bbo_.ask_price = price;
      bbo_.ask_qty = level ? level->total_qty : 0;
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

  level_store_.clear();
  bid_tree_.clear();
  ask_tree_.clear();

  window_stats_ = RingBufferSlidingWindowStats();

  updateBBO();
}

void OrderBook::addOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  size_t idx = 0;
  Order* new_order = order_arena_.allocate(order_id, price, qty, side, &idx);
  if (unlikely(!new_order)) return;
  OrderIndexMap::InsertResult insert_result = order_map_.insert(order_id, idx);
  if (unlikely(insert_result != OrderIndexMap::InsertResult::kInserted)) {
    order_arena_.deallocate(idx);
    return;
  }

  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    update_bid = (bid_tree_.size() == 0) || (price >= bbo_.bid_price);
  } else {
    update_ask = (ask_tree_.size() == 0) || (price <= bbo_.ask_price);
  }

  PriceLevel* level = findPriceLevel(side, price);

  if (unlikely(!level)) {
    level = addPriceLevel(side, price, idx, qty);
    if (unlikely(!level)) {
      order_map_.erase(order_id);
      order_arena_.deallocate(idx);
      return;
    }
    new_order->level_id = level->id;
  } else {
    if (level->order_count == 0) {
      level->head_idx = idx;
      level->tail_idx = idx;
      level->total_qty = qty;
      level->order_count = 1;
      new_order->prev_idx = idx;
      new_order->next_idx = idx;
    } else {
      size_t tail_idx = level->tail_idx;
      size_t head_idx = level->head_idx;
      Order* tail = order_arena_.get(tail_idx);
      Order* head = order_arena_.get(head_idx);
      new_order->prev_idx = tail_idx;
      new_order->next_idx = head_idx;
      tail->next_idx = idx;
      head->prev_idx = idx;
      level->tail_idx = idx;
      level->total_qty += qty;
      level->order_count++;
    }
    new_order->level_id = level->id;
  }

  updateBBOSide(update_bid, update_ask);
  ++order_count_;
}

void OrderBook::modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side) {
  size_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;
  }
  Order* order = order_arena_.get(idx);

  bool update_bid = false;
  bool update_ask = false;

  if (unlikely(order->price != price)) {
    deleteOrder(order_id, side);
    addOrder(order_id, price, qty, side);
    return;
  }

  PriceLevel* level = level_store_.get(order->level_id);
  if (unlikely(!level)) {
    return;
  }

  const int32_t qty_diff = static_cast<int32_t>(qty) - static_cast<int32_t>(order->qty);
  order->qty = qty;
  level->total_qty += qty_diff;

  if (side == Side::BUY) {
    update_bid = (bbo_.bid_price == level->price);
  } else {
    update_ask = (bbo_.ask_price == level->price);
  }

  updateBBOSide(update_bid, update_ask);
}

void OrderBook::deleteOrder(uint64_t order_id, Side side) {
  size_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;
  }
  Order* order = order_arena_.get(idx);

  PriceLevel* level = level_store_.get(order->level_id);
  if (unlikely(!level)) return;

  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    int32_t best_price = 0;
    PriceLevelTree::LevelId best_level = PriceLevelTree::kInvalidLevel;
    if (bid_tree_.max(&best_price, &best_level) && best_price == level->price) {
      update_bid = true;
    }
  } else {
    int32_t best_price = 0;
    PriceLevelTree::LevelId best_level = PriceLevelTree::kInvalidLevel;
    if (ask_tree_.min(&best_price, &best_level) && best_price == level->price) {
      update_ask = true;
    }
  }

  if (level->order_count == 1) {
    removePriceLevel(side, level->price, level->id);
  } else {
    Order* prev = order_arena_.get(order->prev_idx);
    Order* next = order_arena_.get(order->next_idx);
    prev->next_idx = order->next_idx;
    next->prev_idx = order->prev_idx;

    if (level->head_idx == idx) {
      level->head_idx = order->next_idx;
    }
    if (level->tail_idx == idx) {
      level->tail_idx = order->prev_idx;
    }
    level->total_qty -= order->qty;
    if (level->order_count > 0) {
      --level->order_count;
    }
  }

  order_map_.erase(order_id);
  order_arena_.deallocate(idx);
  if (order_count_ > 0) {
    --order_count_;
  }

  updateBBOSide(update_bid, update_ask);
}

void OrderBook::processTrade(uint64_t order_id, uint64_t /*trade_id*/, int32_t price,
                             uint64_t qty, Side side, uint64_t timestamp) {
  size_t idx = 0;
  if (unlikely(!order_map_.find(order_id, &idx))) {
    return;
  }
  Order* order = order_arena_.get(idx);

  window_stats_.recordTrade(timestamp, price, qty);

  bool update_bid = false;
  bool update_ask = false;

  if (side == Side::BUY) {
    update_bid = (bbo_.bid_price == price);
  } else {
    update_ask = (bbo_.ask_price == price);
  }

  if (unlikely(order->qty <= qty)) {
    deleteOrder(order_id, side);
    return;
  }

  order->qty -= static_cast<uint32_t>(qty);
  PriceLevel* level = level_store_.get(order->level_id);
  if (likely(level)) {
    level->total_qty -= static_cast<uint32_t>(qty);
  }

  updateBBOSide(update_bid, update_ask);
}

size_t OrderBook::getBidLevels() const {
  return bid_tree_.size();
}

size_t OrderBook::getAskLevels() const {
  return ask_tree_.size();
}

int32_t OrderBook::getBidPrice(size_t level) const {
  int32_t price = 0;
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  if (!bid_tree_.nthFromMax(level, &price, &level_id)) {
    return 0;
  }
  return price;
}

uint32_t OrderBook::getBidQty(size_t level) const {
  int32_t price = 0;
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  if (!bid_tree_.nthFromMax(level, &price, &level_id)) {
    return 0;
  }
  const PriceLevel* lvl = level_store_.get(level_id);
  return lvl ? lvl->total_qty : 0;
}

int32_t OrderBook::getAskPrice(size_t level) const {
  int32_t price = 0;
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  if (!ask_tree_.nthFromMin(level, &price, &level_id)) {
    return 0;
  }
  return price;
}

uint32_t OrderBook::getAskQty(size_t level) const {
  int32_t price = 0;
  PriceLevelTree::LevelId level_id = PriceLevelTree::kInvalidLevel;
  if (!ask_tree_.nthFromMin(level, &price, &level_id)) {
    return 0;
  }
  const PriceLevel* lvl = level_store_.get(level_id);
  return lvl ? lvl->total_qty : 0;
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

  bid_tree_.forEachFromMax(k, [&](int32_t /*price*/, PriceLevelTree::LevelId level_id) {
    const PriceLevel* lvl = level_store_.get(level_id);
    if (lvl) {
      total_bid_qty += lvl->total_qty;
    }
  });

  ask_tree_.forEachFromMin(k, [&](int32_t /*price*/, PriceLevelTree::LevelId level_id) {
    const PriceLevel* lvl = level_store_.get(level_id);
    if (lvl) {
      total_ask_qty += lvl->total_qty;
    }
  });

  uint64_t total = total_bid_qty + total_ask_qty;
  if (unlikely(total == 0)) return 0.0;

  int64_t diff = static_cast<int64_t>(total_bid_qty) - static_cast<int64_t>(total_ask_qty);
  return static_cast<double>(diff) / static_cast<double>(total);
}

double OrderBook::getBookPressure(size_t k) const {
  double mid = getMidPrice();
  if (unlikely(mid <= 0.0)) return 0.0;

  double bid_pressure = 0.0;
  double ask_pressure = 0.0;

  bid_tree_.forEachFromMax(k, [&](int32_t price, PriceLevelTree::LevelId level_id) {
    double distance = mid - static_cast<double>(price);
    if (likely(distance > 0.0)) {
      const PriceLevel* lvl = level_store_.get(level_id);
      if (lvl) {
        bid_pressure += static_cast<double>(lvl->total_qty) / distance;
      }
    }
  });

  ask_tree_.forEachFromMin(k, [&](int32_t price, PriceLevelTree::LevelId level_id) {
    double distance = static_cast<double>(price) - mid;
    if (likely(distance > 0.0)) {
      const PriceLevel* lvl = level_store_.get(level_id);
      if (lvl) {
        ask_pressure += static_cast<double>(lvl->total_qty) / distance;
      }
    }
  });

  double total = bid_pressure + ask_pressure;
  if (unlikely(total == 0.0)) return 0.0;

  return (bid_pressure - ask_pressure) / total;
}

size_t OrderBook::getOrderRank(uint64_t order_id) const {
  size_t idx = 0;
  if (!order_map_.find(order_id, &idx)) {
    return 0;
  }
  const Order* order = order_arena_.get(idx);

  size_t rank = 1;
  size_t current_idx = order->prev_idx;
  while (current_idx != idx) {
    const Order* current = order_arena_.get(current_idx);
    rank++;
    current_idx = current->prev_idx;
  }

  return rank;
}

uint32_t OrderBook::getQtyAhead(uint64_t order_id) const {
  size_t idx = 0;
  if (!order_map_.find(order_id, &idx)) {
    return 0;
  }
  const Order* order = order_arena_.get(idx);

  uint32_t qty_ahead = 0;
  size_t current_idx = order->prev_idx;
  while (current_idx != idx) {
    const Order* current = order_arena_.get(current_idx);
    qty_ahead += current->qty;
    current_idx = current->prev_idx;
  }

  return qty_ahead;
}

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
