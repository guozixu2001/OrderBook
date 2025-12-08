#pragma once

#include <cstdint>
#include <sys/types.h>

namespace framework {

enum class MessageType : uint16_t {
  ORDERBOOK_CLEAR = 0,
  ADD_ORDER = 1,
  MODIFY_ORDER = 2,
  DELETE_ORDER = 3,
  ADD_TRADE = 4,
};

struct message_header {
  MessageType type;
  uint16_t size;
  uint64_t time; // YYYYMMDDHHMMSS, exchange timestamp
} __attribute__((packed));

struct orderbook_clear : public message_header {
  char symbol[16];
} __attribute__((packed));

struct add_order : public message_header {
  char symbol[16];
  uint64_t order_id;
  int32_t price;
  uint32_t qty;
  uint8_t side;
  uint8_t lot_type;
  uint16_t order_type;
  uint32_t order_book_position;
} __attribute__((packed));

struct modify_order : public message_header {
  char symbol[16];
  uint64_t order_id;
  int32_t price;
  uint32_t qty;
  uint8_t side;
  char filler;
  uint16_t order_type;
  uint32_t order_book_position;
} __attribute__((packed));

struct delete_order : public message_header {
  char symbol[16];
  uint64_t order_id;
  uint8_t side;
  char filler;
} __attribute__((packed));

struct add_trade : public message_header {
  char symbol[16];
  uint64_t order_id;
  int32_t price;
  uint64_t trade_id;
  uint32_t combo_group_id;
  uint8_t side;
  uint8_t deal_type;
  uint16_t trade_condition;
  uint16_t info;
  char filler[2];
  uint64_t qty;
  uint64_t trade_time; // Date and time of the last trade in UTC timestamp
                       // (nanoseconds since 1970) precision to the nearest
                       // 1/100th second
} __attribute__((packed));

enum class ReaderStatus {
  OK,
  PENDING,
  FINISHED,
};
} // namespace framework
