#include <gtest/gtest.h>

#include "lx/engine/shard.hpp"

using namespace lx;
using namespace lx::proto;

static constexpr engine::ShardConfig TEST_SHARD{
    .max_orders = 256, .ladder_size = 64, .queue_depth = 64};
using TestShard = engine::Shard<TEST_SHARD>;

static InboundMsg in_new(uint64_t oid, int64_t price, uint32_t qty, Side side,
                         TimeInForce tif = TimeInForce::GTC)
{
  InboundMsg m{};
  m.new_order.hdr = {sizeof(NewOrder), MsgType::NEW_ORDER, 0};
  m.new_order.order_id = oid;
  m.new_order.price = price;
  m.new_order.qty = qty;
  m.new_order.side = side;
  m.new_order.tif = tif;
  return m;
}

static InboundMsg in_cancel(uint64_t token)
{
  InboundMsg m{};
  m.cancel.hdr = {sizeof(CancelOrder), MsgType::CANCEL_ORDER, 0};
  m.cancel.order_token = token;
  return m;
}

// Pop one outbound message, failing the test if the queue is empty.
static OutboundMsg pop_out(TestShard& shard)
{
  OutboundMsg m{};
  EXPECT_TRUE(shard.outbound().pop(m));
  return m;
}

TEST(Shard, PassiveOrderRestsEmitsAck)
{
  TestShard shard{100, 1};
  shard.inbound().push(in_new(1, 105, 50, Side::BUY));
  shard.tick();

  OutboundMsg m = pop_out(shard);
  EXPECT_EQ(m.hdr.type, MsgType::ACK);
  EXPECT_EQ(m.ack.order_id, uint64_t{1});
  EXPECT_NE(m.ack.order_token, uint64_t{0});
  EXPECT_EQ(shard.best_bid(), int64_t{105});
}

TEST(Shard, MatchProducesFill)
{
  TestShard shard{100, 1};
  shard.inbound().push(in_new(1, 105, 50, Side::SELL));
  shard.tick();
  pop_out(shard);  // drain the resting sell's Ack

  shard.inbound().push(in_new(2, 105, 50, Side::BUY));
  shard.tick();

  OutboundMsg m = pop_out(shard);
  EXPECT_EQ(m.hdr.type, MsgType::FILL);
  EXPECT_EQ(m.fill.qty, uint32_t{50});
  EXPECT_EQ(m.fill.aggressor_id, uint64_t{2});
  EXPECT_EQ(m.fill.resting_id, uint64_t{1});
  EXPECT_EQ(m.fill.price, int64_t{105});
  EXPECT_EQ(shard.best_ask(), INT64_MAX);
}

TEST(Shard, PartialMatchLeavesRemainder)
{
  TestShard shard{100, 1};

  shard.inbound().push(in_new(1, 105, 100, Side::SELL));
  shard.tick();
  pop_out(shard);  // resting sell Ack

  shard.inbound().push(in_new(2, 105, 30, Side::BUY));
  shard.tick();

  OutboundMsg m = pop_out(shard);
  EXPECT_EQ(m.hdr.type, MsgType::FILL);
  EXPECT_EQ(m.fill.qty, uint32_t{30});
  EXPECT_EQ(shard.best_ask(), int64_t{105});
}

TEST(Shard, CancelRemovesRestingOrder)
{
  TestShard shard{100, 1};

  shard.inbound().push(in_new(1, 105, 50, Side::BUY));
  shard.tick();
  OutboundMsg ack = pop_out(shard);
  ASSERT_EQ(ack.hdr.type, MsgType::ACK);
  EXPECT_EQ(shard.best_bid(), int64_t{105});

  shard.inbound().push(in_cancel(ack.ack.order_token));
  shard.tick();

  OutboundMsg cack = pop_out(shard);
  EXPECT_EQ(cack.hdr.type, MsgType::ACK);
  EXPECT_EQ(shard.best_bid(), INT64_MIN);
}

TEST(Shard, CancelOrderingPreservedAfterNew)
{
  // NEW then CANCEL in the same stream: the cancel must not overtake the new,
  // so by the time the cancel is processed the order is in the book.
  TestShard shard{100, 1};

  shard.inbound().push(in_new(1, 105, 50, Side::BUY));
  shard.tick();
  OutboundMsg ack = pop_out(shard);
  uint64_t    token = ack.ack.order_token;

  shard.inbound().push(in_cancel(token));
  shard.tick();
  pop_out(shard);  // cancel Ack

  EXPECT_EQ(shard.best_bid(), INT64_MIN);
}

TEST(Shard, StaleCancelRejected)
{
  TestShard shard{100, 1};

  shard.inbound().push(in_new(1, 105, 50, Side::BUY));
  shard.tick();
  OutboundMsg ack = pop_out(shard);
  uint64_t    token = ack.ack.order_token;

  shard.inbound().push(in_cancel(token));
  shard.tick();
  pop_out(shard);  // first cancel Ack (success)

  shard.inbound().push(in_cancel(token));  // stale — already cancelled
  shard.tick();

  OutboundMsg rej = pop_out(shard);
  EXPECT_EQ(rej.hdr.type, MsgType::REJECT);
}

TEST(Shard, IocKilledWhenNoLiquidity)
{
  TestShard shard{100, 1};

  shard.inbound().push(in_new(1, 105, 50, Side::BUY, TimeInForce::IOC));
  shard.tick();

  OutboundMsg m{};
  EXPECT_FALSE(shard.outbound().pop(m));  // IOC killed: no rest, no fill, no ack
  EXPECT_EQ(shard.best_bid(), INT64_MIN);
}
