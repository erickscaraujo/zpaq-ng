// aligned_array.hpp - Aligned, zero-initialized contiguous storage.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Modern replacement for the legacy libzpaq::Array<T>. Preserves the two
// properties the compression core depends on:
//   1. storage is aligned on a 64 byte boundary (SIMD friendly),
//   2. memory is zero-initialized.
// Unlike the legacy class, resizing never leaks on exception and the wrapper
// is exception safe (RAII). T must be a trivially copyable, trivially
// destructible type such as u8/u16/u32/u64/int.

#ifndef ZPAQ_NG_MEMORY_ALIGNED_ARRAY_HPP
#define ZPAQ_NG_MEMORY_ALIGNED_ARRAY_HPP

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <utility>

#include "core/error.hpp"

namespace zpaq_ng::memory {

namespace detail {

// Allocate n*sizeof(T) bytes aligned to 64 bytes. Returns nullptr if n==0.
inline void* aligned_alloc64(std::size_t bytes) {
  if (bytes == 0) return nullptr;
  void* p = nullptr;
#if defined(_WIN32)
  p = _aligned_malloc(bytes, 64);
#else
  if (::posix_memalign(&p, 64, bytes) != 0) p = nullptr;
#endif
  if (!p) throw memory_error("Out of memory");
  return p;
}

inline void aligned_free64(void* p) noexcept {
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

} // namespace detail

// A 64-byte aligned array of n zero-initialized elements of type T.
// The wrap-around index operator() is valid only when size() is a power of two.
template <typename T>
class aligned_array {
public:
  aligned_array() = default;

  explicit aligned_array(std::size_t n) : data_(nullptr), size_(0) {
    resize(n);
  }

  aligned_array(std::size_t n, int exponent) : data_(nullptr), size_(0) {
    resize(n, exponent);
  }

  aligned_array(const aligned_array&) = delete;
  aligned_array& operator=(const aligned_array&) = delete;

  aligned_array(aligned_array&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}

  aligned_array& operator=(aligned_array&& other) noexcept {
    if (this != &other) {
      detail::aligned_free64(data_);
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  ~aligned_array() { detail::aligned_free64(data_); }

  // Change size to sz << ex elements, all zeroed. Old contents are freed.
  void resize(std::size_t sz, int ex = 0) {
    while (ex > 0) {
      if (sz > sz * 2) throw memory_error("Array too big");
      sz *= 2;
      --ex;
    }
    T* new_data = static_cast<T*>(detail::aligned_alloc64(sz * sizeof(T)));
    detail::aligned_free64(data_);
    data_ = new_data;
    size_ = sz;
    if (data_ && size_) {
      // Zero initialize without calling constructors (T is trivial).
      std::memset(data_, 0, size_ * sizeof(T));
    }
  }

  // Number of elements.
  std::size_t size() const noexcept { return size_; }

  // Number of elements as a (narrowing) signed int.
  int isize() const noexcept { return static_cast<int>(size_); }

  // Bounds checked element access.
  T& operator[](std::size_t i) noexcept {
    assert(i < size_);
    return data_[i];
  }
  const T& operator[](std::size_t i) const noexcept {
    assert(i < size_);
    return data_[i];
  }

  // Wrap-around element access. Requires size() to be a power of two.
  T& operator()(std::size_t i) noexcept {
    assert((size_ & (size_ - 1)) == 0);
    return data_[i & (size_ - 1)];
  }
  const T& operator()(std::size_t i) const noexcept {
    assert((size_ & (size_ - 1)) == 0);
    return data_[i & (size_ - 1)];
  }

  // Raw pointer (may be nullptr when empty).
  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }

private:
  T* data_ = nullptr;
  std::size_t size_ = 0;
};

} // namespace zpaq_ng::memory

#endif // ZPAQ_NG_MEMORY_ALIGNED_ARRAY_HPP