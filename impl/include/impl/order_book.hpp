#pragma once

#include <cstdint>
#include <cstring>
#include <climits>
#include <limits>
#include <memory>
#include <new>
#include <vector>

#include "impl/price_level_tree.hpp"
#include "impl/sliding_window_ring.hpp"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace impl {

struct Order;
struct PriceLevel;

constexpr size_t kOrderMapInitialCapacity = 1u << 16;  // Initial hint, not a limit
constexpr size_t SYMBOL_LEN = 16;          // Symbol length
constexpr size_t INVALID_INDEX = std::numeric_limits<size_t>::max();

enum class Side : uint8_t {
  BUY = 0,
  SELL = 1
};

struct alignas(64) BBO {
  int32_t bid_price = 0;
  uint32_t bid_qty = 0;
  int32_t ask_price = 0;
  uint32_t ask_qty = 0;
};

struct Order {
  uint64_t order_id = 0;
  int32_t price = 0;
  uint32_t qty = 0;
  Side side = Side::BUY;
  size_t level_id = INVALID_INDEX;

  size_t prev_idx = INVALID_INDEX;
  size_t next_idx = INVALID_INDEX;

  Order(uint64_t id, int32_t p, uint32_t q, Side s)
    : order_id(id), price(p), qty(q), side(s), level_id(INVALID_INDEX),
      prev_idx(INVALID_INDEX), next_idx(INVALID_INDEX) {}

  Order() = default;
  Order(const Order&) = delete;
  Order& operator=(const Order&) = delete;
};

struct PriceLevel {
  size_t id = INVALID_INDEX;
  int32_t price = 0;
  Side side = Side::BUY;
  uint32_t total_qty = 0;  // Sum of all order quantities at this level
  size_t order_count = 0;  // Number of orders at this level

  size_t head_idx = INVALID_INDEX;  // First order in FIFO
  size_t tail_idx = INVALID_INDEX;  // Last order in FIFO

  PriceLevel(int32_t p, Side s, size_t order_idx, uint32_t qty, size_t level_id)
    : id(level_id), price(p), side(s), total_qty(qty), order_count(1),
      head_idx(order_idx), tail_idx(order_idx) {}

  PriceLevel() = default;
  PriceLevel(const PriceLevel&) = default;
  PriceLevel& operator=(const PriceLevel&) = default;
};

class OrderIndexMap {
public:
  enum class InsertResult : uint8_t {
    kInserted = 0,
    kExists = 1,
    kFailed = 2,
  };

  explicit OrderIndexMap(size_t initial_capacity = kOrderMapInitialCapacity);
  void clear();
  bool find(uint64_t key, size_t* idx_out) const;
  InsertResult insert(uint64_t key, size_t idx);
  bool erase(uint64_t key);
  size_t size() const { return size_; }

private:
  static constexpr uint8_t kEmpty = 0x80;
  static constexpr uint8_t kTombstone = 0xFE;
  static constexpr size_t kGroupSize = 16;
  static constexpr size_t kMinCapacity = 16;

  static size_t nextPow2(size_t value);
  void rehash(size_t new_capacity);
  InsertResult insertInternal(uint64_t key, size_t idx);

  std::vector<uint8_t> ctrl_;
  std::vector<uint64_t> keys_;
  std::vector<size_t> idxs_;
  size_t capacity_ = 0;
  size_t size_ = 0;
  size_t tombstones_ = 0;
};

class OrderArena {
public:
  static constexpr size_t kChunkShift = 16;
  static constexpr size_t kChunkSize = 1u << kChunkShift;
  static constexpr size_t kChunkMask = kChunkSize - 1;

  OrderArena() = default;
  ~OrderArena() = default;

  Order* allocate(uint64_t order_id, int32_t price, uint32_t qty, Side side, size_t* out_idx);
  void deallocate(size_t idx);
  Order* get(size_t idx) const;
  void clear();

private:
  bool addChunk();

  std::vector<std::unique_ptr<Order[]>> chunks_;
  std::vector<size_t> free_list_;
  size_t next_index_ = 0;
  size_t capacity_ = 0;
};

class PriceLevelStore {
public:
  using LevelId = size_t;

  LevelId allocate(int32_t price, Side side, size_t order_idx, uint32_t qty);
  void deallocate(LevelId id);
  PriceLevel* get(LevelId id);
  const PriceLevel* get(LevelId id) const;
  void clear();

private:
  std::vector<PriceLevel> levels_;
  std::vector<LevelId> free_list_;
};

class OrderBook {
private:
  char symbol_[SYMBOL_LEN];

  OrderArena order_arena_;
  OrderIndexMap order_map_{kOrderMapInitialCapacity};

  PriceLevelStore level_store_;
  PriceLevelTree bid_tree_;
  PriceLevelTree ask_tree_;

  BBO bbo_;

  RingBufferSlidingWindowStats window_stats_;

  size_t order_count_ = 0;

  PriceLevel* findPriceLevel(Side side, int32_t price);
  const PriceLevel* findPriceLevel(Side side, int32_t price) const;
  PriceLevel* addPriceLevel(Side side, int32_t price, size_t order_idx, uint32_t qty);
  void removePriceLevel(Side side, int32_t price, size_t level_id);

  void updateBBO();
  void updateBBOSide(bool update_bid, bool update_ask);

public:
  explicit OrderBook(const char* symbol);
  OrderBook() : symbol_{""} {
    bbo_ = {};
  }
  ~OrderBook();

  void setSymbol(const char* symbol) {
    std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
    symbol_[SYMBOL_LEN - 1] = '\0';
  }

  void clear();
  void addOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side);
  void modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side);
  void deleteOrder(uint64_t order_id, Side side);
  void processTrade(uint64_t order_id, uint64_t trade_id, int32_t price,
                    uint64_t qty, Side side, uint64_t timestamp);

  // Query functions
  const BBO* getBBO() const { return &bbo_; }
  size_t getBidLevels() const;
  size_t getAskLevels() const;
  int32_t getBidPrice(size_t level) const;
  uint32_t getBidQty(size_t level) const;
  int32_t getAskPrice(size_t level) const;
  uint32_t getAskQty(size_t level) const;

  // Metrics calculations
  double getMidPrice() const;
  int32_t getSpread() const;
  double getMacroPrice() const;
  double getImbalance(size_t k) const;
  double getBookPressure(size_t k) const;

  // Order tracking
  size_t getOrderRank(uint64_t order_id) const;
  uint32_t getQtyAhead(uint64_t order_id) const;

  // Time window metrics (10 minutes)
  void evictExpiredTrades(uint64_t current_timestamp);
  int32_t getPriceRange() const;
  uint64_t getWindowVolume() const;
  uint64_t getWindowAmount() const;
  uint64_t getVWAP() const;
  int32_t getMedianPrice() const;
  int32_t getVWAPLevel() const;

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;
};

} // namespace impl
