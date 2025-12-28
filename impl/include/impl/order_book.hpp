#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <climits>

namespace impl {

struct Order;
struct PriceLevel;

constexpr size_t MAX_PRICE_LEVELS = 2048;  // Maximum number of price levels
constexpr size_t MAX_ORDERS = 65536;       // Maximum number of orders
constexpr size_t SYMBOL_LEN = 16;          // Symbol length

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

struct alignas(64) Order {
  uint64_t order_id = 0;
  int32_t price = 0;
  uint32_t qty = 0;
  Side side = Side::BUY;

  Order* prev = nullptr;
  Order* next = nullptr;

  Order(uint64_t id, int32_t p, uint32_t q, Side s)
    : order_id(id), price(p), qty(q), side(s), prev(this), next(this) {}

  Order() = default;
  Order(const Order&) = delete;
  Order& operator=(const Order&) = delete;
};

struct alignas(64) PriceLevel {
  int32_t price = 0;
  Side side = Side::BUY;
  uint32_t total_qty = 0;     // Sum of all order quantities at this level
  size_t order_count = 0;     // Number of orders at this level

  Order* first_order = nullptr;  // First order in the FIFO queue

  // oubly linked list of price levels
  PriceLevel* prev = nullptr;
  PriceLevel* next = nullptr;

  PriceLevel(int32_t p, Side s, Order* order)
    : price(p), side(s), first_order(order) {
    updateQty();
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

template<typename T, size_t N>
class MemoryPool {
private:
  alignas(64) std::array<T, N> storage_;
  alignas(64) std::array<bool, N> used_;
  size_t next_free_ = 0;

public:
  MemoryPool() {
    used_.fill(false);
  }

  template<typename... Args>
  T* allocate(Args&&... args) {
    // Simple linear search for free slot
    // TODO: we could optimize further with a free list
    size_t start = next_free_;
    do {
      if (!used_[next_free_]) {
        used_[next_free_] = true;
        T* obj = &storage_[next_free_];
        new (obj) T(std::forward<Args>(args)...);  // Placement new
        next_free_ = (next_free_ + 1) % N;
        return obj;
      }
      next_free_ = (next_free_ + 1) % N;
    } while (next_free_ != start);

    return nullptr;  // Pool exhausted
  }

  void deallocate(T* obj) {
    size_t index = obj - &storage_[0];
    if (index < N) {
      obj->~T();  
      used_[index] = false;
    }
  }

  bool contains(const T* obj) const {
    return obj >= &storage_[0] && obj < &storage_[0] + N;
  }

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;
};

// Forward declaration for OrderBook
class OrderBook;

// Sliding window statistics for time-based metrics
// Pre-allocated ring buffer for zero-allocation operation
class alignas(64) SlidingWindowStats {
private:
  static constexpr size_t MAX_TRADES = 65536;      // Max trades in 10 minute window
  static constexpr size_t WINDOW_SECONDS = 600;    // 10 minutes
  static constexpr size_t SECONDARY_BUCKETS = 601; // 600 + 1 for boundary

  // Trade data (SoA layout for cache efficiency)
  alignas(64) std::array<uint64_t, MAX_TRADES> timestamps_;  // Trade timestamps
  alignas(64) std::array<int32_t, MAX_TRADES> prices_;       // Trade prices
  alignas(64) std::array<uint64_t, MAX_TRADES> quantities_;  // Trade quantities
  alignas(64) std::array<uint64_t, MAX_TRADES> amounts_;     // price * quantity

  // Ring buffer pointers
  size_t head_ = 0;    // Next write position
  size_t count_ = 0;   // Current number of trades in window

  // Incremental statistics
  uint64_t sum_qty_ = 0;      // Sum of quantities
  uint64_t sum_amount_ = 0;   // Sum of amounts (price * qty)
  mutable int32_t min_price_ = INT32_MAX;  // Updated from heaps when needed
  mutable int32_t max_price_ = INT32_MIN;  // Updated from heaps when needed

  // Secondary index: timestamp -> trade index for fast eviction
  // Each bucket represents 1 second, stores the first trade in that second
  alignas(64) std::array<size_t, SECONDARY_BUCKETS> sec_index_;
  alignas(64) std::array<size_t, SECONDARY_BUCKETS> sec_count_;
  uint64_t base_timestamp_ = 0;

  // Pre-allocated cache for median calculation
  mutable std::array<int32_t, MAX_TRADES> price_cache_;

  // Min/max maintenance using heaps with lazy deletion
  // Each heap stores trade indices, ordered by price
  alignas(64) std::array<size_t, MAX_TRADES> max_heap_;  // Max-heap (largest price at root)
  alignas(64) std::array<size_t, MAX_TRADES> min_heap_;  // Min-heap (smallest price at root)
  size_t max_heap_size_ = 0;
  size_t min_heap_size_ = 0;

  // Position of each trade in the heaps (SIZE_MAX if not in heap)
  alignas(64) std::array<size_t, MAX_TRADES> max_heap_pos_;
  alignas(64) std::array<size_t, MAX_TRADES> min_heap_pos_;

  // Validity flags for lazy deletion
  alignas(64) std::array<bool, MAX_TRADES> valid_;

  // Heap helper functions
  void pushToMaxHeap(size_t trade_idx);
  void pushToMinHeap(size_t trade_idx);
  void removeFromMaxHeap(size_t trade_idx);
  void removeFromMinHeap(size_t trade_idx);
  void rebuildHeapsIfNeeded();
  void updateMinMaxFromHeaps() const;

public:
  SlidingWindowStats();

  // Record a trade (O(1) amortized)
  void recordTrade(uint64_t timestamp, int32_t price, uint64_t qty);

  // Remove expired trades older than 10 minutes from current timestamp
  void evictExpired(uint64_t current_timestamp);

  // Query functions
  int32_t getPriceRange() const {
    if (count_ == 0) return 0;
    updateMinMaxFromHeaps();
    return max_price_ - min_price_;
  }

  uint64_t getTotalVolume() const { return sum_qty_; }
  uint64_t getTotalAmount() const { return sum_amount_; }

  uint64_t getVWAP() const {
    if (count_ == 0 || sum_qty_ == 0) return 0;
    return sum_amount_ / sum_qty_;
  }

  // Get median price (O(n) but using pre-allocated cache)
  int32_t getMedianPrice() const;

  // Get VWAP level (which price level the VWAP falls into)
  int32_t getVWAPLevel(const OrderBook* ob) const;
};

class OrderBook {
private:
  char symbol_[SYMBOL_LEN];

  // Memory pools for pre-allocated objects (allocated on heap at construction)
  // Using pointers to avoid large stack allocation
  MemoryPool<Order, MAX_ORDERS>* order_pool_ = nullptr;
  MemoryPool<PriceLevel, MAX_PRICE_LEVELS>* level_pool_ = nullptr;

  // Order tracking: order_id -> Order* (O(1) lookup)
  OrderHashMap order_map_;

  // Price level tracking: price -> PriceLevel* (O(1) lookup)
  PriceLevelHashMap price_level_map_;

  // Price levels: doubly linked lists sorted by price
  PriceLevel* bids_ = nullptr;  // Best bid (highest price)
  PriceLevel* asks_ = nullptr;  // Best ask (lowest price)

  // Best Bid/Offer cache
  BBO bbo_;

  // Sliding window statistics for trade-based metrics
  SlidingWindowStats window_stats_;

  // Find a price level by price (O(1) using hash map)
  PriceLevel* findPriceLevel(int32_t price) const;

  // Convert price to hash index
  size_t priceToIndex(int32_t price) const;

  // Add a price level to the book
  void addPriceLevel(PriceLevel* level);

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

    order_pool_ = new MemoryPool<Order, MAX_ORDERS>();
    level_pool_ = new MemoryPool<PriceLevel, MAX_PRICE_LEVELS>();
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
