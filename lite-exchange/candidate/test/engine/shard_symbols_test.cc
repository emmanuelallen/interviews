#include <gtest/gtest.h>

#include "lx/engine/shard.hpp"

using namespace lx;
using namespace lx::proto;

static constexpr uint16_t MAX_SYMBOLS = 8;

static constexpr engine::ShardConfig TEST_SHARD{
    .max_orders = 256, .ladder_size = 64, .queue_depth = 64, .max_symbols = MAX_SYMBOLS};

static constexpr int64_t  BASE_PRICE = 100;
static constexpr int64_t  TICK_SIZE = 1;
static constexpr int64_t  REST_PRICE = 105;
static constexpr int64_t  HIGHER_PRICE = 107;
static constexpr uint32_t QTY = 50;

// Distinct symbols that all live on the SAME shard — exactly what symbol_id % N
// produces once symbols outnumber shards.
static constexpr uint16_t SYMBOL_A = 0;
static constexpr uint16_t SYMBOL_B = 3;
static constexpr uint16_t SYMBOL_C = 5;
static constexpr uint16_t SYMBOL_UNTOUCHED = 1;
static constexpr uint16_t SYMBOL_OUT_OF_RANGE = MAX_SYMBOLS;

using TestShard = engine::Shard<TEST_SHARD>;

static InboundMsg in_new(uint16_t symbol, uint64_t oid, int64_t price, uint32_t qty, Side side)
{
  InboundMsg m{};
  m.new_order.hdr = {sizeof(NewOrder), MsgType::NEW_ORDER, 0};
  m.new_order.symbol_id = symbol;
  m.new_order.order_id = oid;
  m.new_order.price = price;
  m.new_order.qty = qty;
  m.new_order.side = side;
  m.new_order.tif = TimeInForce::GTC;
  return m;
}

static InboundMsg in_cancel(uint64_t token)
{
  InboundMsg m{};
  m.cancel.hdr = {sizeof(CancelOrder), MsgType::CANCEL_ORDER, 0};
  m.cancel.order_token = token;
  return m;
}

static OutboundMsg pop_out(TestShard& shard)
{
  OutboundMsg m{};
  EXPECT_TRUE(shard.outbound().pop(m));
  return m;
}

// The core defect: two symbols sharing a shard must not trade with each other.
TEST(ShardSymbols, SymbolsOnSameShardDoNotCrossMatch)
{
  TestShard shard{BASE_PRICE, TICK_SIZE};

  shard.inbound().push(in_new(SYMBOL_A, 1, REST_PRICE, QTY, Side::SELL));
  shard.tick();
  EXPECT_EQ(pop_out(shard).hdr.type, MsgType::ACK);

  // Same price, crossing side, different symbol: must rest, not fill.
  shard.inbound().push(in_new(SYMBOL_B, 2, REST_PRICE, QTY, Side::BUY));
  shard.tick();

  OutboundMsg m = pop_out(shard);
  EXPECT_EQ(m.hdr.type, MsgType::ACK);
  EXPECT_EQ(m.ack.order_id, uint64_t{2});

  // Both orders still resting, each in its own book.
  EXPECT_EQ(shard.best_ask(SYMBOL_A), REST_PRICE);
  EXPECT_EQ(shard.best_bid(SYMBOL_B), REST_PRICE);
}

TEST(ShardSymbols, BestPricesArePerSymbol)
{
  TestShard shard{BASE_PRICE, TICK_SIZE};

  shard.inbound().push(in_new(SYMBOL_A, 1, REST_PRICE, QTY, Side::BUY));
  shard.tick();
  shard.inbound().push(in_new(SYMBOL_B, 2, HIGHER_PRICE, QTY, Side::BUY));
  shard.tick();

  EXPECT_EQ(shard.best_bid(SYMBOL_A), REST_PRICE);
  EXPECT_EQ(shard.best_bid(SYMBOL_B), HIGHER_PRICE);
  EXPECT_EQ(shard.best_bid(SYMBOL_UNTOUCHED), INT64_MIN);
}

// A cancel carries only a token — the shard must still find the owning book.
TEST(ShardSymbols, CancelResolvesOwningSymbolBook)
{
  TestShard shard{BASE_PRICE, TICK_SIZE};

  shard.inbound().push(in_new(SYMBOL_C, 1, REST_PRICE, QTY, Side::BUY));
  shard.tick();
  OutboundMsg ack = pop_out(shard);
  ASSERT_EQ(ack.hdr.type, MsgType::ACK);
  uint64_t token = ack.ack.order_token;

  shard.inbound().push(in_cancel(token));
  shard.tick();

  EXPECT_EQ(pop_out(shard).hdr.type, MsgType::ACK);
  EXPECT_EQ(shard.best_bid(SYMBOL_C), INT64_MIN);
}

TEST(ShardSymbols, SymbolOutOfRangeRejected)
{
  TestShard shard{BASE_PRICE, TICK_SIZE};

  shard.inbound().push(in_new(SYMBOL_OUT_OF_RANGE, 1, REST_PRICE, QTY, Side::BUY));
  shard.tick();

  OutboundMsg m = pop_out(shard);
  EXPECT_EQ(m.hdr.type, MsgType::REJECT);
  EXPECT_EQ(m.reject.order_id, uint64_t{1});
  EXPECT_EQ(m.reject.reason, RejectReason::UNKNOWN_SYMBOL);
}
