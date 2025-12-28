#pragma once

#include <array>
#include <cstring>
#include <cstdlib>

#include "framework/define.hpp"
#include "impl/order_book.hpp"
#include "framework/logger.hpp"

namespace impl {

template <class Reader, class Gateway> class Impl {
private:
  Reader &reader;
  Gateway &gateway;
  std::vector<int64_t> grids;
  size_t grid_idx_ = 0;  // Current grid index
  uint64_t last_msg_time_ = 0;  // Last processed message timestamp

  // Maximum number of different symbols/order books we can track
  static constexpr size_t MAX_ORDER_BOOKS = 100;

  // Hash map from symbol -> OrderBook using fixed-size array
  std::array<OrderBook*, MAX_ORDER_BOOKS> order_books_;

  // Memory pool for order books (allocated on heap during initialization)
  alignas(OrderBook) char* order_book_storage_ = nullptr;

  // Symbol to order book mapping
  // Store symbol hash for fast comparison, and symbol string for collision resolution
  struct SymbolMapping {
    size_t hash = 0;
    char symbol[SYMBOL_LEN] = {0};
    bool used = false;
  };
  std::array<SymbolMapping, MAX_ORDER_BOOKS> symbol_mapping_;
  size_t num_used_order_books_ = 0;

  size_t hashSymbol(const char* symbol) const {
    size_t hash = 0;
    for (size_t i = 0; symbol[i] != '\0' && i < 16; i++) {
      hash = hash * 31 + static_cast<size_t>(symbol[i]);
    }
    return hash;
  }

  OrderBook* getOrderBook(const char* symbol) {
    size_t hash = hashSymbol(symbol);
    size_t index = hash % MAX_ORDER_BOOKS;

    // Check if an order book is already assigned to this symbol at this index
    if (symbol_mapping_[index].used && symbol_mapping_[index].hash == hash) {
      // Hash matches, verify symbol string (in case of hash collision)
      if (std::strncmp(symbol_mapping_[index].symbol, symbol, SYMBOL_LEN) == 0) {
        return order_books_[index];
      }
      // Hash collision - need to find another slot
      return findOrderBookForSymbol(symbol, hash);
    }

    // No order book assigned to this symbol yet - assign one from pre-allocated pool
    return assignOrderBookToSymbol(symbol, hash, index);
  }

  // Find order book for symbol in case of hash collision
  OrderBook* findOrderBookForSymbol(const char* symbol, size_t hash) {
    // Linear search for symbol (should be rare with good hash function)
    for (size_t i = 0; i < MAX_ORDER_BOOKS; i++) {
      if (symbol_mapping_[i].used && symbol_mapping_[i].hash == hash) {
        // Hash matches, check symbol string
        if (std::strncmp(symbol_mapping_[i].symbol, symbol, SYMBOL_LEN) == 0) {
          return order_books_[i];
        }
      }
    }
    return nullptr;  // Symbol not found
  }

  // Assign a pre-allocated order book to a symbol
  OrderBook* assignOrderBookToSymbol(const char* symbol, size_t hash, size_t preferred_index) {
    if (num_used_order_books_ >= MAX_ORDER_BOOKS) {
      return nullptr;  // All order books are in use
    }

    // Try to use the preferred index first if available
    if (order_books_[preferred_index] != nullptr && !symbol_mapping_[preferred_index].used) {
      // This order book exists but isn't assigned to any symbol yet
      symbol_mapping_[preferred_index].hash = hash;
      std::strncpy(symbol_mapping_[preferred_index].symbol, symbol, SYMBOL_LEN - 1);
      symbol_mapping_[preferred_index].symbol[SYMBOL_LEN - 1] = '\0';
      symbol_mapping_[preferred_index].used = true;
      order_books_[preferred_index]->setSymbol(symbol);
      num_used_order_books_++;
      return order_books_[preferred_index];
    }

    // Find first available order book
    for (size_t i = 0; i < MAX_ORDER_BOOKS; i++) {
      if (order_books_[i] != nullptr && !symbol_mapping_[i].used) {
        symbol_mapping_[i].hash = hash;
        std::strncpy(symbol_mapping_[i].symbol, symbol, SYMBOL_LEN - 1);
        symbol_mapping_[i].symbol[SYMBOL_LEN - 1] = '\0';
        symbol_mapping_[i].used = true;
        order_books_[i]->setSymbol(symbol);
        num_used_order_books_++;
        return order_books_[i];
      }
    }

    return nullptr;  // No available order books
  }

  void processMessage(const framework::message_header* msg) {
    LOG_DEBUG("processMessage called with msg=%p, type=%d, size=%u",
            (const void*)msg, static_cast<int>(msg->type), msg->size);

    switch (msg->type) {
      case framework::MessageType::ORDERBOOK_CLEAR: {
        LOG_DEBUG("Processing ORDERBOOK_CLEAR");
        auto* clear_msg = static_cast<const framework::orderbook_clear*>(msg);
        LOG_DEBUG("symbol=%.16s", clear_msg->symbol);
        OrderBook* ob = getOrderBook(clear_msg->symbol);
        LOG_DEBUG("ob=%p", (void*)ob);
        if (ob) {
          ob->clear();
        }
        break;
      }

      case framework::MessageType::ADD_ORDER: {
        LOG_DEBUG("Processing ADD_ORDER");
        auto* add_msg = static_cast<const framework::add_order*>(msg);
        LOG_DEBUG("symbol=%.16s, order_id=%lu, price=%d, qty=%u, side=%u",
                add_msg->symbol, add_msg->order_id, add_msg->price, add_msg->qty, add_msg->side);
        OrderBook* ob = getOrderBook(add_msg->symbol);
        LOG_DEBUG("ob=%p", (void*)ob);
        if (ob) {
          impl::Side side = (add_msg->side == 0) ? impl::Side::BUY : impl::Side::SELL;
          ob->addOrder(add_msg->order_id, add_msg->price, add_msg->qty, side);
        }
        break;
      }

      case framework::MessageType::MODIFY_ORDER: {
        LOG_DEBUG("Processing MODIFY_ORDER");
        auto* mod_msg = static_cast<const framework::modify_order*>(msg);
        LOG_DEBUG("symbol=%.16s, order_id=%lu, price=%d, qty=%u, side=%u",
                mod_msg->symbol, mod_msg->order_id, mod_msg->price, mod_msg->qty, mod_msg->side);
        OrderBook* ob = getOrderBook(mod_msg->symbol);
        LOG_DEBUG("ob=%p", (void*)ob);
        if (ob) {
          impl::Side side = (mod_msg->side == 0) ? impl::Side::BUY : impl::Side::SELL;
          ob->modifyOrder(mod_msg->order_id, mod_msg->price, mod_msg->qty, side);
        }
        break;
      }

      case framework::MessageType::DELETE_ORDER: {
        LOG_DEBUG("Processing DELETE_ORDER");
        auto* del_msg = static_cast<const framework::delete_order*>(msg);
        LOG_DEBUG("symbol=%.16s, order_id=%lu, side=%u",
                del_msg->symbol, del_msg->order_id, del_msg->side);
        OrderBook* ob = getOrderBook(del_msg->symbol);
        LOG_DEBUG("ob=%p", (void*)ob);
        if (ob) {
          impl::Side side = (del_msg->side == 0) ? impl::Side::BUY : impl::Side::SELL;
          ob->deleteOrder(del_msg->order_id, side);
        }
        break;
      }

      case framework::MessageType::ADD_TRADE: {
        LOG_DEBUG("Processing ADD_TRADE");
        auto* trade_msg = static_cast<const framework::add_trade*>(msg);
        LOG_DEBUG("symbol=%.16s, order_id=%lu, trade_id=%lu, price=%d, qty=%lu, side=%u, time=%lu",
                trade_msg->symbol, trade_msg->order_id, trade_msg->trade_id, trade_msg->price,
                trade_msg->qty, trade_msg->side, trade_msg->trade_time);
        OrderBook* ob = getOrderBook(trade_msg->symbol);
        LOG_DEBUG("ob=%p", (void*)ob);
        if (ob) {
          impl::Side side = (trade_msg->side == 0) ? impl::Side::BUY : impl::Side::SELL;
          ob->processTrade(trade_msg->order_id, trade_msg->trade_id, trade_msg->price,
                           trade_msg->qty, side, trade_msg->trade_time);
        }
        break;
      }

      default: {
        LOG_DEBUG("Unknown message type: %d", static_cast<int>(msg->type));
        break;
      }
    }

    LOG_DEBUG("processMessage completed for type=%d", static_cast<int>(msg->type));
  }

  // Calculate and signal metrics via gateway
  void signalMetrics(OrderBook* ob, const char* symbol, uint64_t time) {
    const BBO* bbo = ob->getBBO();

    LOG_DEBUG("BBO: symbol=%s, bid_price=%d, bid_qty=%u, ask_price=%d, ask_qty=%u",
            symbol, bbo->bid_price, bbo->bid_qty, bbo->ask_price, bbo->ask_qty);

    // Signal mid price
    double mid_price = ob->getMidPrice();
    if (mid_price > 0.0) {
      LOG_DEBUG("%s.mid_price = %f at time %lu", symbol, mid_price, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.mid_price", symbol);
      gateway.signal(metric_name, symbol, time, mid_price);
    }

    // Signal spread
    int32_t spread = ob->getSpread();
    if (spread > 0) {
      LOG_DEBUG("%s.spread = %d at time %lu", symbol, spread, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.spread", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(spread));
    }

    // Signal macro price
    double macro_price = ob->getMacroPrice();
    if (macro_price > 0.0) {
      LOG_DEBUG("%s.macro_price = %f at time %lu", symbol, macro_price, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.macro_price", symbol);
      gateway.signal(metric_name, symbol, time, macro_price);
    }

    // Signal imbalance for K=5 and K=10
    double imbalance_5 = ob->getImbalance(5);
    if (imbalance_5 != 0.0 || (bbo->bid_qty > 0 && bbo->ask_qty > 0)) {
      LOG_DEBUG("%s.imbalance_5 = %f at time %lu", symbol, imbalance_5, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.imbalance_5", symbol);
      gateway.signal(metric_name, symbol, time, imbalance_5);
    }

    double imbalance_10 = ob->getImbalance(10);
    if (imbalance_10 != 0.0 || (bbo->bid_qty > 0 && bbo->ask_qty > 0)) {
      LOG_DEBUG("%s.imbalance_10 = %f at time %lu", symbol, imbalance_10, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.imbalance_10", symbol);
      gateway.signal(metric_name, symbol, time, imbalance_10);
    }

    // Signal book pressure for K=5 and K=10
    double pressure_5 = ob->getBookPressure(5);
    if (pressure_5 != 0.0 || (bbo->bid_qty > 0 && bbo->ask_qty > 0)) {
      LOG_DEBUG("%s.pressure_5 = %f at time %lu", symbol, pressure_5, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.pressure_5", symbol);
      gateway.signal(metric_name, symbol, time, pressure_5);
    }

    double pressure_10 = ob->getBookPressure(10);
    if (pressure_10 != 0.0 || (bbo->bid_qty > 0 && bbo->ask_qty > 0)) {
      LOG_DEBUG("%s.pressure_10 = %f at time %lu", symbol, pressure_10, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.pressure_10", symbol);
      gateway.signal(metric_name, symbol, time, pressure_10);
    }

    // Evict expired trades based on grid time (in seconds)
    ob->evictExpiredTrades(time);

    // Signal time window metrics (10 minutes)
    // 4.6: Price range (high - low) in past 10 minutes
    int32_t price_range = ob->getPriceRange();
    if (price_range >= 0) {
      LOG_DEBUG("%s.price_range_10min = %d at time %lu", symbol, price_range, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.price_range_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(price_range));
    }

    // 4.7: Volume and amount in past 10 minutes
    uint64_t window_volume = ob->getWindowVolume();
    if (window_volume > 0) {
      LOG_DEBUG("%s.volume_10min = %lu at time %lu", symbol, window_volume, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.volume_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(window_volume));
    }

    uint64_t window_amount = ob->getWindowAmount();
    if (window_amount > 0) {
      LOG_DEBUG("%s.amount_10min = %lu at time %lu", symbol, window_amount, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.amount_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(window_amount));
    }

    // 4.8: VWAP in past 10 minutes
    uint64_t vwap = ob->getVWAP();
    if (vwap > 0) {
      LOG_DEBUG("%s.vwap_10min = %lu at time %lu", symbol, vwap, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.vwap_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(vwap));
    }

    // 4.9: Median price in past 10 minutes
    int32_t median_price = ob->getMedianPrice();
    if (median_price > 0) {
      LOG_DEBUG("%s.median_price_10min = %d at time %lu", symbol, median_price, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.median_price_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(median_price));
    }

    // 4.10: VWAP level (which price level VWAP falls into)
    int32_t vwap_level = ob->getVWAPLevel();
    if (vwap_level != 0) {
      LOG_DEBUG("%s.vwap_level_10min = %d at time %lu", symbol, vwap_level, time);
      char metric_name[256];
      snprintf(metric_name, sizeof(metric_name), "%s.vwap_level_10min", symbol);
      gateway.signal(metric_name, symbol, time, static_cast<double>(vwap_level));
    }
  }

public:
  Impl(Reader &reader, Gateway &gateway, const std::vector<int64_t>& grids) 
    : reader{reader}, gateway{gateway}, grids(grids) {
    LOG_DEBUG("Impl constructor");
    order_books_.fill(nullptr);

    // Allocate memory pool for order books on heap during initialization
    if (posix_memalign(reinterpret_cast<void**>(&order_book_storage_),
                       alignof(OrderBook),
                       MAX_ORDER_BOOKS * sizeof(OrderBook)) != 0) {
      LOG_ERROR("Failed to allocate memory for order book storage");
      abort();
    }
    LOG_DEBUG("order_book_storage allocated on heap");

    // Pre-allocate ALL OrderBook objects upfront 
    for (size_t i = 0; i < MAX_ORDER_BOOKS; i++) {
      OrderBook* ob = reinterpret_cast<OrderBook*>(order_book_storage_ + i * sizeof(OrderBook));
      new (ob) OrderBook();  
      order_books_[i] = ob;
    }
    LOG_DEBUG("All %zu OrderBook objects pre-allocated", MAX_ORDER_BOOKS);
  }

  Impl(Impl &&other) noexcept = delete;

  Impl &operator=(Impl &&other) noexcept = delete;

  ~Impl() {
    // Clean up ALL pre-allocated order books
    for (size_t i = 0; i < MAX_ORDER_BOOKS; i++) {
      if (order_books_[i] != nullptr) {
        order_books_[i]->~OrderBook();
        order_books_[i] = nullptr;
      }
    }

    // Free the allocated memory
    free(order_book_storage_);
    order_book_storage_ = nullptr;
  }

  void run() {
    LOG_DEBUG("Impl::run() started");

    while (true) {
      auto [status, data, size] = reader.try_get_tick();

      if (status == framework::ReaderStatus::FINISHED) {
        LOG_DEBUG("ReaderStatus::FINISHED");

        // Process any pending grid signal for the last timestamp
        if (grid_idx_ < grids.size() && last_msg_time_ == grids[grid_idx_]) {
          LOG_DEBUG("Signalling at final grid time %lu", grids[grid_idx_]);
          signalAllOrderBooks(grids[grid_idx_]);
        }
        break;
      }

      if (status == framework::ReaderStatus::OK && data && size > 0) {
        auto* msg = reinterpret_cast<const framework::message_header*>(data);
        uint64_t msg_time = msg->time;

        LOG_DEBUG("ReaderStatus::OK, data=%p, size=%zu, time=%lu", (const void*)data, size, msg_time);

        // Check if timestamp changed (end of previous timestamp group)
        if (last_msg_time_ != 0 && msg_time != last_msg_time_) {
          LOG_DEBUG("Timestamp changed: %lu -> %lu", last_msg_time_, msg_time);

          // Process grid that match the previous timestamp
          if (grid_idx_ < grids.size() && last_msg_time_ == grids[grid_idx_]) {
            LOG_DEBUG("Signalling at grid time %lu (last message of group)", grids[grid_idx_]);
            signalAllOrderBooks(grids[grid_idx_]);
            grid_idx_++;
          }
        }

        // Process the message 
        processMessage(msg);
        last_msg_time_ = msg_time;

      } else {
        LOG_DEBUG("ReaderStatus::OK but data is null or size=0");
      }
    }

    LOG_DEBUG("Impl::run() completed");
  }

  void signalAllOrderBooks(int64_t grid_time) {
    size_t signalled_count = 0;
    for (size_t i = 0; i < MAX_ORDER_BOOKS; i++) {
      if (symbol_mapping_[i].used) {
        const char* symbol = symbol_mapping_[i].symbol;
        OrderBook* ob = order_books_[i];

        LOG_DEBUG("Signalling symbol %s at grid time %lu", symbol, grid_time);
        signalMetrics(ob, symbol, grid_time);
        signalled_count++;
      }
    }
    LOG_DEBUG("Completed signalling for %zu symbols at grid time %lu", signalled_count, grid_time);
  }
};
} // namespace impl
