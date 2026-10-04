#pragma once
#include <cstdint>

#include "lx/book/order.hpp"

namespace lx::book
{
template <uint32_t N>
class LevelBitmap
{
 public:
  void set(uint32_t idx) { words_[idx >> 6] |= (1ULL << (idx & 63)); }
  void clear(uint32_t idx) { words_[idx >> 6] &= ~(1ULL << (idx & 63)); }
  bool test(uint32_t idx) const { return words_[idx >> 6] & (1ULL << (idx & 63)); }

  // lowest set bit >= from, else NULL_IDX
  uint32_t next_up(uint32_t from) const
  {
    if (from >= N)
      return NULL_IDX;
    uint32_t w = from >> 6;
    uint64_t bits = words_[w] & (~0ULL << (from & 63));  // clear bits below 'from'
    while (true)
    {
      if (bits)
        return (w << 6) + static_cast<uint32_t>(__builtin_ctzll(bits));
      if (++w >= NUM_WORDS)
        return NULL_IDX;
      bits = words_[w];
    }
  }

  // Highest set bit <= from, else NULL_IDX. (from == NULL_IDX falls through to NULL_IDX.)
  uint32_t next_down(uint32_t from) const
  {
    if (from >= N)
      return NULL_IDX;
    uint32_t w = from >> 6;
    uint64_t bits = words_[w] & (~0ULL >> (63 - (from & 63)));  // clear bits above `from`
    while (true)
    {
      if (bits)
        return (w << 6) + (63u - static_cast<uint32_t>(__builtin_clzll(bits)));
      if (w == 0)
        return NULL_IDX;
      bits = words_[--w];
    }
  }

 private:
  static constexpr uint32_t NUM_WORDS = (N + 63) / 64;
  uint64_t                  words_[NUM_WORDS]{};
};
}  // namespace lx::book