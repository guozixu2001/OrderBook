#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <climits>

#include "impl/memory_pool.hpp"
#include "impl/tiered_memory_pool.hpp"
#include "impl/sliding_window_ring.hpp"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace impl {

struct Order;
struct PriceLevel;

constexpr size_t MAX_PRICE_LEVELS = 2048;  // Maximum number of price levels
constexpr size_t MAX_ORDERS = 65536;       // Maximum number of orders
constexpr size_t SYMBOL_LEN = 16;          // Symbol length

// Hash map type aliases (actual alignment handled by OrderBook members)
using OrderHashMap = std::array<Order*, MAX_ORDERS>;
using PriceLevelHashMap = std::array<PriceLevel*, MAX_PRICE_LEVELS>;

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
  PriceLevel* level = nullptr;

  Order* prev = nullptr;
  Order* next = nullptr;

  Order(uint64_t id, int32_t p, uint32_t q, Side s)
    : order_id(id), price(p), qty(q), side(s), level(nullptr), prev(this), next(this) {}

  Order() = default;
  Order(const Order&) = delete;
  Order& operator=(const Order&) = delete;
};

struct PriceLevel {
  int32_t price = 0;
  Side side = Side::BUY;
  uint32_t total_qty = 0;     // Sum of all order quantities at this level
  size_t order_count = 0;     // Number of orders at this level

  Order* first_order = nullptr;  // First order in the FIFO queue

  // oubly linked list of price levels
  PriceLevel* prev = nullptr;
  PriceLevel* next = nullptr;

  PriceLevel(int32_t p, Side s, Order* order)
    : price(p), side(s), total_qty(order->qty), order_count(1), first_order(order) {
    prev = this;
    next = this;
  }

  PriceLevel() = default;
  PriceLevel(const PriceLevel&) = delete;
  PriceLevel& operator=(const PriceLevel&) = delete;

  // Update total quantity based on all orders
  void updateQty() {
    total_qty = 0;
    order_count = 0;
    if (!first_order) return;

    Order* current = first_order;
    do {
      total_qty += current->qty;
      order_count++;
      current = current->next;
    } while (current != first_order);
  }
};

class OrderBook {
private:
  char symbol_[SYMBOL_LEN];

  // Tiered memory pools for scalable capacity
  // L0 (hot tier) handles normal load, L1+ (cold tiers) handle overflow
  // All tiers pre-allocated at construction - no heap allocation on hot path
  TieredMemoryPool<Order, MAX_ORDERS>* order_pool_ = nullptr;
  TieredMemoryPool<PriceLevel, MAX_PRICE_LEVELS>* level_pool_ = nullptr;

  // Order tracking: order_id -> Order* (cache line aligned to prevent false sharing)
  alignas(64) OrderHashMap order_map_;

  // Price level tracking: price -> PriceLevel* (cache line aligned to prevent false sharing)
  alignas(64) PriceLevelHashMap price_level_map_;

  // Price levels: doubly linked lists sorted by price
  PriceLevel* bids_ = nullptr;  // Best bid 
  PriceLevel* asks_ = nullptr;  // Best ask 

  // Best Bid/Offer cache
  BBO bbo_;

  // Sliding window statistics for trade-based metrics (RingBuffer optimized)
  RingBufferSlidingWindowStats window_stats_;

  // Occupancy counters to avoid full-table scans
  size_t order_count_ = 0;
  size_t price_level_count_ = 0;

  // Convert price to hash index
  size_t priceToIndex(int32_t price) const;

  // Add a price level to the book; returns false if the hash table is full
  bool addPriceLevel(PriceLevel* level);

  // Remove a price level from the book
  void removePriceLevel(PriceLevel* level);

  // Update BBO (full update)
  void updateBBO();

  // Update BBO only for specific side when needed
  void updateBBOSide(bool update_bid, bool update_ask);

public:
  explicit OrderBook(const char* symbol);
  OrderBook() : symbol_{""} {
    order_map_.fill(nullptr);
    price_level_map_.fill(nullptr);
    bbo_ = {};

    // 16 cold tiers allows up to ~1M orders (65536 * 17)
    order_pool_ = new TieredMemoryPool<Order, MAX_ORDERS>(16);
    // 8 cold tiers allows up to ~18K price levels (2048 * 9)
    level_pool_ = new TieredMemoryPool<PriceLevel, MAX_PRICE_LEVELS>(8);
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
  // Find a price level by price (O(1) using hash map)
  PriceLevel* findPriceLevel(int32_t price) const;
  // Get number of price levels
  size_t getBidLevels() const;
  size_t getAskLevels() const;
  // Get price and quantity at specific level
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
  size_t getOrderRank(uint64_t order_id) const;  // 1-based rank
  uint32_t getQtyAhead(uint64_t order_id) const; // Quantity ahead in queue

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
