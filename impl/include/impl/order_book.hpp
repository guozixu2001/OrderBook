#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace impl {

// Forward declarations
struct Order;
struct PriceLevel;

// Constants - configure based on requirements
constexpr size_t MAX_PRICE_LEVELS = 2048;  // Maximum number of price levels
constexpr size_t MAX_ORDERS = 65536;       // Maximum number of orders
constexpr size_t SYMBOL_LEN = 16;          // Symbol length

// Hash map types for O(1) lookups
using OrderHashMap = std::array<Order*, MAX_ORDERS>;
using PriceLevelHashMap = std::array<PriceLevel*, MAX_PRICE_LEVELS>;

// Order side
enum class Side : uint8_t {
  BUY = 0,
  SELL = 1
};

// Best Bid/Offer (BBO) structure
struct alignas(64) BBO {
  int32_t bid_price = 0;
  uint32_t bid_qty = 0;
  int32_t ask_price = 0;
  uint32_t ask_qty = 0;
};

// Order structure - intrusive doubly linked list (no allocation for nodes)
struct alignas(64) Order {
  uint64_t order_id = 0;
  int32_t price = 0;
  uint32_t qty = 0;
  Side side = Side::BUY;

  // Intrusive pointers for linked list at same price level
  Order* prev = nullptr;
  Order* next = nullptr;

  // Constructor
  Order(uint64_t id, int32_t p, uint32_t q, Side s)
    : order_id(id), price(p), qty(q), side(s), prev(this), next(this) {}

  Order() = default;
  Order(const Order&) = delete;
  Order& operator=(const Order&) = delete;
};

// Price level structure - contains all orders at a specific price
struct alignas(64) PriceLevel {
  int32_t price = 0;
  Side side = Side::BUY;
  uint32_t total_qty = 0;     // Sum of all order quantities at this level
  size_t order_count = 0;     // Number of orders at this level

  Order* first_order = nullptr;  // First order in the FIFO queue

  // Intrusive pointers for doubly linked list of price levels
  PriceLevel* prev = nullptr;
  PriceLevel* next = nullptr;

  // Constructor
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

// Memory pool for pre-allocation (no heap allocation in hot path)
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

  // Allocate an object from the pool
  template<typename... Args>
  T* allocate(Args&&... args) {
    // Simple linear search for free slot
    // For HFT, we could optimize further with a free list
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

  // Deallocate an object back to the pool
  void deallocate(T* obj) {
    size_t index = obj - &storage_[0];
    if (index < N) {
      obj->~T();  // Call destructor
      used_[index] = false;
    }
  }

  // Check if object belongs to this pool
  bool contains(const T* obj) const {
    return obj >= &storage_[0] && obj < &storage_[0] + N;
  }

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;
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

    // Allocate memory pools on heap (during initialization, not hot path)
    order_pool_ = new MemoryPool<Order, MAX_ORDERS>();
    level_pool_ = new MemoryPool<PriceLevel, MAX_PRICE_LEVELS>();
  }
  ~OrderBook();

  // Set symbol after construction (for pre-allocated order books)
  void setSymbol(const char* symbol) {
    std::strncpy(symbol_, symbol, SYMBOL_LEN - 1);
    symbol_[SYMBOL_LEN - 1] = '\0';
  }

  // Process message types
  void clear();                                          // Clear entire book
  void addOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side);
  void modifyOrder(uint64_t order_id, int32_t price, uint32_t qty, Side side);
  void deleteOrder(uint64_t order_id, Side side);
  void processTrade(uint64_t order_id, uint64_t trade_id, int32_t price,
                    uint64_t qty, Side side);

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

  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;
};

} // namespace impl
