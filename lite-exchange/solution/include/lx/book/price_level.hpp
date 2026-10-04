#pragma once
#include <cstdint>

#include "lx/book/order.hpp"

namespace lx::book
{
struct PriceLevel
{
  uint32_t head_idx = NULL_IDX;
  uint32_t tail_idx = NULL_IDX;
  uint32_t order_count = 0;
  uint32_t total_qty = 0;

  bool empty() const { return head_idx == NULL_IDX; }

  void push_back(uint32_t idx, Order* base)
  {
    Order& o = base[idx];
    o.prev_idx = tail_idx;
    o.next_idx = NULL_IDX;

    if (tail_idx != NULL_IDX)
      base[tail_idx].next_idx = idx;
    else
      head_idx = idx;

    tail_idx = idx;
    ++order_count;
    total_qty += o.qty;
  }

  uint32_t pop_front(Order* base)
  {
    if (head_idx == NULL_IDX)
      return NULL_IDX;

    uint32_t idx = head_idx;
    Order&   o = base[idx];
    head_idx = o.next_idx;

    if (head_idx != NULL_IDX)
      base[head_idx].prev_idx = NULL_IDX;
    else
      tail_idx = NULL_IDX;

    --order_count;
    total_qty -= o.qty;
    return idx;
  }

  void remove(uint32_t idx, Order* base)
  {
    Order& o = base[idx];

    if (o.prev_idx != NULL_IDX)
      base[o.prev_idx].next_idx = o.next_idx;
    else
      head_idx = o.next_idx;

    if (o.next_idx != NULL_IDX)
      base[o.next_idx].prev_idx = o.prev_idx;
    else
      tail_idx = o.prev_idx;

    --order_count;
    total_qty -= o.qty;
  }
};
}  // namespace lx::book