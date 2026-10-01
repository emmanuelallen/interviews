#pragma once
#include <array>
#include <cstdint>

#include "lx/book/order_book.hpp"
#include "lx/proto/messages.hpp"

namespace lx::engine
{
// Routes each inbound message to one of N shards: new orders by symbol, cancels
// by the shard id packed into their token. A given symbol always lands on the
// same shard, preserving per-symbol FIFO. push() returns the target shard's
// backpressure result so Session::consume can use it as its sink.
template <typename Shard, uint32_t N>
class ShardRouter
{
 public:
  explicit ShardRouter(std::array<Shard*, N> shards) : shards_(shards) {}

  bool push(const proto::InboundMsg& msg)
  {
    uint32_t s;
    if (msg.hdr.type == proto::MsgType::NEW_ORDER)
      s = msg.new_order.symbol_id % N;
    else if (msg.hdr.type == proto::MsgType::CANCEL_ORDER)
      s = book::OrderHandle::token_shard(msg.cancel.order_token) % N;
    else
      return true;  // unknown type: skip without stalling consume
    return shards_[s]->inbound().push(msg);
  }

 private:
  std::array<Shard*, N> shards_;
};
}  // namespace lx::engine
