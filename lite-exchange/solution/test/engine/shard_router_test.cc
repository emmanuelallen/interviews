#include "lx/engine/shard_router.hpp"

#include <gtest/gtest.h>

#include "lx/engine/shard.hpp"

using namespace lx;
using namespace lx::proto;

// Two symbols split across two shards: each shard hosts ONE book but must be
// addressable by either global id, which is what the symbol table buys.
static constexpr engine::ShardConfig TEST_SHARD{
    .max_orders = 256, .ladder_size = 64, .queue_depth = 64, .max_symbols = 1, .symbol_space = 2};
using S = engine::Shard<TEST_SHARD>;

static InboundMsg new_order(uint16_t symbol, uint64_t oid, int64_t price, Side side)
{
  InboundMsg m{};
  m.new_order.hdr = {sizeof(NewOrder), MsgType::NEW_ORDER, 0};
  m.new_order.symbol_id = symbol;
  m.new_order.order_id = oid;
  m.new_order.price = price;
  m.new_order.qty = 10;
  m.new_order.side = side;
  m.new_order.tif = TimeInForce::GTC;
  return m;
}

TEST(ShardRouter, RoutesBySymbol)
{
  S                         s0{100, 1, 0, {0}};
  S                         s1{100, 1, 1, {1}};
  engine::ShardRouter<S, 2> router{{&s0, &s1}};

  router.push(new_order(/*symbol*/ 0, 1, 105, Side::BUY));  // -> shard 0
  router.push(new_order(/*symbol*/ 1, 2, 107, Side::BUY));  // -> shard 1
  s0.tick();
  s1.tick();

  EXPECT_EQ(s0.best_bid(/*symbol*/ 0), int64_t{105});
  EXPECT_EQ(s1.best_bid(/*symbol*/ 1), int64_t{107});
}

TEST(ShardRouter, RoutesCancelByTokenShard)
{
  S                         s0{100, 1, 0, {0}};
  S                         s1{100, 1, 1, {1}};
  engine::ShardRouter<S, 2> router{{&s0, &s1}};

  // Rest an order on shard 1; read its Ack to get the shard-tagged token.
  router.push(new_order(1, 42, 107, Side::BUY));
  s1.tick();
  OutboundMsg ack{};
  ASSERT_TRUE(s1.outbound().pop(ack));
  ASSERT_EQ(ack.hdr.type, MsgType::ACK);
  ASSERT_EQ(book::OrderHandle::token_shard(ack.ack.order_token), 1u);

  // Cancel carries only the token; the router must route it back to shard 1.
  InboundMsg cxl{};
  cxl.cancel.hdr = {sizeof(CancelOrder), MsgType::CANCEL_ORDER, 0};
  cxl.cancel.order_token = ack.ack.order_token;
  router.push(cxl);
  s1.tick();

  EXPECT_EQ(s1.best_bid(/*symbol*/ 1), INT64_MIN);
}
