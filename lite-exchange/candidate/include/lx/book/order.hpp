#pragma once
#include <cstdint>
#include <type_traits>

#include "lx/proto/messages.hpp"

namespace lx::book
{
static constexpr uint32_t NULL_IDX = UINT32_MAX;

struct Order
{
  uint64_t           order_id;
  int64_t            price;
  uint32_t           qty;
  uint32_t           next_idx;
  uint32_t           prev_idx;
  uint16_t           symbol;
  proto::Side        side;
  proto::TimeInForce tif;
};

static_assert(sizeof(Order) == 32);
static_assert(std::is_trivially_copyable_v<Order>);
}  // namespace lx::book