#pragma once
#include <cstdint>
#include <unordered_map>

#include "lx/book/order.hpp"

namespace lx::book
{
// SOLUTION (part 3): per-account doubly linked list of resting orders,
// threaded through slot-indexed side arrays. Shard-wide, like the arena and
// order_session_: the shard owns it, every book borrows it, so one account's
// orders across all books are on one list. Order itself stays 32 bytes.
template <uint32_t N>
class AccountIndex
{
 public:
  AccountIndex()
  {
    for (uint32_t i = 0; i < N; ++i)
      next_[i] = prev_[i] = NULL_IDX;
  }

  // Push-front, O(1). Allocates only the first time a session rests an order.
  void link(uint32_t slot, uint32_t session)
  {
    uint32_t& head = head_.try_emplace(session, NULL_IDX).first->second;
    prev_[slot] = NULL_IDX;
    next_[slot] = head;
    if (head != NULL_IDX)
      prev_[head] = slot;
    head = slot;
  }

  // O(1). Must run for every resting order that leaves the book: cancel AND fill.
  void unlink(uint32_t slot, uint32_t session)
  {
    if (prev_[slot] != NULL_IDX)
      next_[prev_[slot]] = next_[slot];
    else
      head_[session] = next_[slot];
    if (next_[slot] != NULL_IDX)
      prev_[next_[slot]] = prev_[slot];
    next_[slot] = prev_[slot] = NULL_IDX;
  }

  uint32_t first(uint32_t session) const
  {
    auto it = head_.find(session);
    return it == head_.end() ? NULL_IDX : it->second;
  }
  uint32_t next(uint32_t slot) const { return next_[slot]; }

 private:
  uint32_t                               next_[N];
  uint32_t                               prev_[N];
  std::unordered_map<uint32_t, uint32_t> head_;
};
}  // namespace lx::book
