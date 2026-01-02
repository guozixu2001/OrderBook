#pragma once

#include <array>
#include <cstdint>

// Branch prediction hints for compiler optimization
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace impl {

// MemoryPool: Index-based Free List with prefetch
// Uses a separate array of indices for better cache locality
// Adds prefetch hint for next allocation to reduce cache misses
// O(1) allocate/deallocate with minimal cache misses
template<typename T, size_t N>
class MemoryPool {
private:
  alignas(64) std::array<T, N> storage_;
  alignas(64) std::array<uint32_t, N> free_;
  uint32_t free_count_;

public:
  MemoryPool() : free_count_(static_cast<uint32_t>(N)) {
    for (uint32_t i = 0; i < N; ++i) {
      free_[i] = i;
    }
  }

  template<typename... Args>
  T* allocate(Args&&... args) {
    if (unlikely(free_count_ == 0)) {
      return nullptr;
    }

    uint32_t idx = free_[--free_count_];
    T* obj = &storage_[idx];

    // Prefetch the next allocation target if available
    if (likely(free_count_ > 0)) {
      uint32_t next_idx = free_[free_count_ - 1];
      __builtin_prefetch(&storage_[next_idx], 0, 1);  // Read prefetch
    }

    new (obj) T(std::forward<Args>(args)...);
    return obj;
  }

  void deallocate(T* obj) {
    size_t idx = obj - &storage_[0];
    obj->~T();
    free_[free_count_++] = static_cast<uint32_t>(idx);
  }

  bool contains(const T* obj) const {
    return obj >= &storage_[0] && obj < &storage_[0] + N;
  }

  size_t freeCount() const { return free_count_; }

  MemoryPool(const MemoryPool&) = delete;
  MemoryPool& operator=(const MemoryPool&) = delete;
};

} // namespace impl
