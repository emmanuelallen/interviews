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
    return arena_.get()->free_list[--free_top_];
  }

  void free(uint32_t idx)
  {
    assert(idx < N);
    Storage* st = arena_.get();
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

 private:
  struct Storage
  {
    T        slots[N];
    uint32_t free_list[N];
    uint32_t gen[N];
  };

  lx::util::HugePage<Storage, 1> arena_;
  uint32_t                       free_top_;
};
}  // namespace lx::book
