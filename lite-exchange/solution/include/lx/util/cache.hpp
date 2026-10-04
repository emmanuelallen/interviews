#pragma once
#include <cstddef>

namespace lx {
inline constexpr std::size_t kCacheLine = 64;

// Pads T onto its own cache line to avoid false sharing.

template <typename T> struct alignas(kCacheLine) CachePadded {
  T value{};
};

static_assert(sizeof(CachePadded<char>) == kCacheLine);
}; // namespace lx