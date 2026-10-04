#pragma once
#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <new>

namespace lx::util
{
static constexpr std::size_t HUGE_PAGE_SIZE = 2ULL * 1024 * 1024;

template <typename T, uint32_t N>
class HugePage
{
 public:
  HugePage()
  {
    std::size_t const bytes = align_up(N * sizeof(T), HUGE_PAGE_SIZE);
    ptr_ = static_cast<T*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0));
    if (ptr_ == MAP_FAILED)
    {
      ptr_ = static_cast<T*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
      if (ptr_ == MAP_FAILED)
        throw std::bad_alloc{};
    }
    bytes_ = bytes;
  }

  ~HugePage() { munmap(ptr_, bytes_); }

  HugePage(const HugePage&)            = delete;
  HugePage& operator=(const HugePage&) = delete;

  T&       operator[](uint32_t i)       { return ptr_[i]; }
  const T& operator[](uint32_t i) const { return ptr_[i]; }
  T*       get()                        { return ptr_; }
  const T* get()                  const { return ptr_; }

 private:
  static std::size_t align_up(std::size_t n, std::size_t align)
  {
    return (n + align - 1) & ~(align - 1);
  }

  T*          ptr_   = nullptr;
  std::size_t bytes_ = 0;
};
}  // namespace lx::util
