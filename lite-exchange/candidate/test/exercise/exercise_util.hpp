#pragma once
// Helpers shared by the exercise tests. Same style as test/engine/engine_test.cc.
#include <gtest/gtest.h>

#include <vector>

#include "lx/book/order_book.hpp"
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
                         uint32_t qty, TimeInForce tif = TimeInForce::GTC, uint64_t expire_at = 0)
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
  m.new_order.expire_at = expire_at;
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

inline InboundMsg in_timer(uint64_t now_ns)
{
  InboundMsg m{};
  m.timer.hdr = {sizeof(Timer), MsgType::TIMER, 0};
  m.timer.now_ns = now_ns;
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
                     int64_t price, uint32_t qty, uint64_t expire_at = 0)
{
  auto acks = of(send(shard, in_new(session, symbol, oid, side, price, qty, TimeInForce::GTC,
                                    expire_at)),
                 MsgType::ACK, session);
  EXPECT_EQ(acks.size(), 1u);
  return acks.empty() ? 0 : acks[0].ack.order_token;
}

// A book with storage of its own, as in test/book/book_test.cc.
template <uint32_t MAX_ORDERS>
struct BookStorage
{
  book::Pool<book::Order, MAX_ORDERS> pool;
  uint32_t                            order_session[MAX_ORDERS]{};
};

template <uint32_t MAX_ORDERS, uint32_t LADDER_SIZE>
struct StandaloneBook : private BookStorage<MAX_ORDERS>,
                        public book::OrderBook<MAX_ORDERS, LADDER_SIZE>
{
  StandaloneBook(int64_t base_price, int64_t tick_size)
      : book::OrderBook<MAX_ORDERS, LADDER_SIZE>(base_price, tick_size, this->pool,
                                                 this->order_session)
  {
  }
};

inline NewOrder book_order(uint64_t oid, Side side, int64_t price, uint32_t qty)
{
  return in_new(0, 0, oid, side, price, qty).new_order;
}
}  // namespace ex
