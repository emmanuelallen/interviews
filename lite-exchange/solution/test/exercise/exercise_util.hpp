#pragma once
// Helpers shared by the exercise tests. Same style as test/engine/engine_test.cc.
#include <gtest/gtest.h>

#include <vector>

#include "lx/engine/shard.hpp"

namespace ex
{
using namespace lx;
using namespace lx::proto;

constexpr uint32_t SESSION_A = 1;
constexpr uint32_t SESSION_B = 2;

// Two instruments on one shard, sharing one order arena (as in the repo).
inline constexpr engine::ShardConfig EX_SHARD{
    .max_orders = 256, .ladder_size = 64, .queue_depth = 1024, .max_symbols = 2};
using ExShard = engine::Shard<EX_SHARD>;
constexpr int64_t BASE = 100;

inline InboundMsg in_new(uint32_t session, uint16_t symbol, uint64_t oid, Side side, int64_t price,
                         uint32_t qty, TimeInForce tif = TimeInForce::GTC)
{
  InboundMsg m{};
  m.new_order.hdr = {sizeof(NewOrder), MsgType::NEW_ORDER, 0};
  m.new_order.hdr.session_id = session;  // the gateway stamps this in production
  m.new_order.symbol_id = symbol;
  m.new_order.order_id = oid;
  m.new_order.price = price;
  m.new_order.qty = qty;
  m.new_order.side = side;
  m.new_order.tif = tif;
  return m;
}

inline InboundMsg in_cancel(uint32_t session, uint64_t token)
{
  InboundMsg m{};
  m.cancel.hdr = {sizeof(CancelOrder), MsgType::CANCEL_ORDER, 0};
  m.cancel.hdr.session_id = session;
  m.cancel.order_token = token;
  return m;
}

inline InboundMsg in_mass_cancel(uint32_t session)
{
  InboundMsg m{};
  m.mass_cancel.hdr = {sizeof(MassCancel), MsgType::MASS_CANCEL, 0};
  m.mass_cancel.hdr.session_id = session;
  return m;
}

// Push one message, run the shard once, return everything it emitted.
inline std::vector<OutboundMsg> send(ExShard& shard, const InboundMsg& msg)
{
  EXPECT_TRUE(shard.inbound().push(msg));
  shard.tick();
  std::vector<OutboundMsg> out;
  OutboundMsg              m{};
  while (shard.outbound().pop(m))
    out.push_back(m);
  return out;
}

// Messages of one type addressed to one session.
inline std::vector<OutboundMsg> of(const std::vector<OutboundMsg>& out, MsgType type,
                                   uint32_t session)
{
  std::vector<OutboundMsg> r;
  for (const auto& m : out)
    if (m.hdr.type == type && m.hdr.session_id == session)
      r.push_back(m);
  return r;
}

// Place an order expected to rest; returns its cancel token (0 if it didn't).
inline uint64_t rest(ExShard& shard, uint32_t session, uint16_t symbol, uint64_t oid, Side side,
                     int64_t price, uint32_t qty)
{
  auto acks = of(send(shard, in_new(session, symbol, oid, side, price, qty)), MsgType::ACK, session);
  EXPECT_EQ(acks.size(), 1u);
  return acks.empty() ? 0 : acks[0].ack.order_token;
}

}  // namespace ex
