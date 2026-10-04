#pragma once
#include <cassert>
#include <cstdint>

#include "lx/util/huge_page.hpp"

namespace lx::book
{
template <typename T, uint32_t N>
class Pool
{
 public:
  static constexpr uint32_t INVALID_IDX = UINT32_MAX;
  Pool()
  {
    Storage* st = arena_.get();
    for (uint32_t i = 0; i < N; ++i)
      st->free_list[i] = i;
    free_top_ = N;
  }

  uint32_t alloc()
  {
    if (free_top_ == 0)
      return INVALID_IDX;
    Storage* st = arena_.get();
    uint32_t idx = st->free_list[--free_top_];
    st->live[idx] = 1;  // SOLUTION (bonus)
    return idx;
  }

  void free(uint32_t idx)
  {
    assert(idx < N);
    Storage* st = arena_.get();
    assert(st->live[idx]);  // SOLUTION (bonus): a double free puts idx on the list twice
    st->live[idx] = 0;
    st->gen[idx]++;
    st->free_list[free_top_++] = idx;
  }

  T& operator[](uint32_t idx)
  {
    assert(idx < N);
    return arena_.get()->slots[idx];
  }

  const T& operator[](uint32_t idx) const
  {
    assert(idx < N);
    return arena_.get()->slots[idx];
  }

  T* data() { return arena_.get()->slots; }
  const T* data() const { return arena_.get()->slots; }

  uint32_t capacity() const { return N; }
  uint32_t free_count() const { return free_top_; }
  uint32_t gen(uint32_t idx) const { return arena_.get()->gen[idx]; }
  // SOLUTION (bonus): gen alone can't tell a live slot from a free one; a
  // never-used slot has gen 0, so token 0 used to pass validation.
  bool live(uint32_t idx) const { return arena_.get()->live[idx]; }

 private:
  struct Storage
  {
    T        slots[N];
    uint32_t free_list[N];
    uint8_t  live[N];  // SOLUTION (bonus); the mmap'd arena starts zeroed
    uint32_t gen[N];
  };

  lx::util::HugePage<Storage, 1> arena_;
  uint32_t                       free_top_;
};
}  // namespace lx::book
