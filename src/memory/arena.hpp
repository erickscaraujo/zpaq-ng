// arena.hpp - A simple arena (bump) allocator for short-lived buffers.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The compression pipeline processes many blocks whose intermediate buffers
// are recycled between jobs. A thread-local arena avoids repeated allocation
// churn and keeps peak memory bounded.

#ifndef ZPAQ_NG_MEMORY_ARENA_HPP
#define ZPAQ_NG_MEMORY_ARENA_HPP

#include <cstddef>
#include <cstdlib>
#include <vector>
#include <memory>

#include "core/error.hpp"

namespace zpaq_ng::memory {

// Owns one or more large buffers and hands out byte ranges from the current
// block. Buffers are freed on destruction or clear().
class arena {
public:
  explicit arena(std::size_t block_size = 1u << 20) : block_size_(block_size) {}

  arena(const arena&) = delete;
  arena& operator=(const arena&) = delete;

  arena(arena&&) = default;
  arena& operator=(arena&&) = default;

  // Return a zero-initialized byte range of at least n bytes.
  byte* allocate(std::size_t n) {
    if (offset_ + n > capacity_) grow(n);
    byte* p = current_ + offset_;
    std::memset(p, 0, n);
    offset_ += n;
    return p;
  }

  // Return a typed array of n zero-initialized elements.
  template <typename T>
  T* allocate_typed(std::size_t n) {
    static_assert(std::is_trivially_destructible_v<T>);
    const std::size_t bytes = n * sizeof(T);
    void* raw = allocate(bytes);
    return static_cast<T*>(raw);
  }

  // Reset the cursor without freeing memory (fast path for recycled buffers).
  void reset() noexcept { offset_ = 0; }

  // Free all memory.
  void clear() {
    blocks_.clear();
    current_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
  }

private:
  void grow(std::size_t needed) {
    std::size_t size = block_size_;
    if (needed > size) size = needed;
    byte* b = static_cast<byte*>(std::calloc(size, 1));
    if (!b) throw memory_error("Out of memory");
    blocks_.push_back(std::unique_ptr<byte[]>(b));
    current_ = blocks_.back().get();
    capacity_ = size;
    offset_ = 0;
  }

  std::size_t block_size_;
  std::vector<std::unique_ptr<byte[]>> blocks_;
  byte* current_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t offset_ = 0;
};

} // namespace zpaq_ng::memory

#endif // ZPAQ_NG_MEMORY_ARENA_HPP