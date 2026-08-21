// chunking.cpp - Content-defined chunking implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "chunking/chunking.hpp"

#include <algorithm>
#include <cstring>

namespace zpaq_ng::chunking {

RollingHash::RollingHash(std::size_t window) : window_(window) {
  buffer_.assign(window_, 0);
  pw_.resize(window_);
  std::uint64_t p = 1;
  for (std::size_t i = 0; i < window_; ++i) {
    pw_[i] = p;
    p *= B;
  }
}

namespace {

// Number of bits such that (1u << bits) ~ target_chunk.
unsigned mask_bits(std::size_t target) {
  unsigned b = 0;
  while ((std::size_t{1} << (b + 1)) <= target) ++b;
  if (b > 32) b = 32;
  return b;
}

} // namespace

std::size_t next_boundary(ConstBytes data, std::size_t start,
                          std::size_t min_chunk, std::size_t target_chunk,
                          std::size_t max_chunk) {
  const std::size_t n = data.size();
  if (start >= n) return n;
  const std::size_t lo = std::min(n, start + std::max<std::size_t>(min_chunk, 1));
  const std::size_t hi = std::min(n, start + max_chunk);
  if (lo >= hi) return hi;  // window too small: clamp

  const unsigned bits = mask_bits(target_chunk);
  const std::uint64_t mask = (1ULL << bits) - 1;
  RollingHash rh(32);
  for (std::size_t i = lo; i < hi; ++i) {
    const std::uint64_t h = rh.push(data[i]);
    if ((h & mask) == 0) return i + 1;
  }
  return hi;
}

std::vector<std::size_t> split(ConstBytes data, std::size_t min_chunk,
                               std::size_t target_chunk,
                               std::size_t max_chunk) {
  std::vector<std::size_t> out;
  out.push_back(0);
  std::size_t pos = 0;
  const std::size_t n = data.size();
  while (pos < n) {
    const std::size_t b = next_boundary(data, pos, min_chunk, target_chunk, max_chunk);
    out.push_back(b);
    pos = b;
  }
  return out;
}

} // namespace zpaq_ng::chunking