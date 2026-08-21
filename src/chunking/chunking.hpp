// chunking.hpp - Content-defined chunking (rolling hash).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Content-defined chunking splits a byte stream at boundaries chosen by a
// rolling hash of the local content, so that identical regions align even when
// data is inserted or deleted earlier in the stream. This is the foundation of
// the improved deduplication path and of adaptive block sizing.

#ifndef ZPAQ_NG_CHUNKING_CHUNKING_HPP
#define ZPAQ_NG_CHUNKING_CHUNKING_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/types.hpp"

namespace zpaq_ng::chunking {

// Rolling 64-bit window hash (multiplicative, mod 2^64).
class RollingHash {
public:
  // window is the number of bytes that influence the hash (e.g. 32).
  explicit RollingHash(std::size_t window = 32);

  // Reset the hash to process a new region.
  void reset() { h_ = 0; in_ = 0; }

  // Advance the window by one byte (the next byte of the stream).
  std::uint64_t push(unsigned char c) {
    if (in_ >= window_) h_ -= pw_[window_ - 1] * buffer_[in_ % window_];
    buffer_[in_ % window_] = c;
    h_ = h_ * B + c;
    ++in_;
    return h_;
  }

  std::uint64_t value() const noexcept { return h_; }
  std::size_t window() const noexcept { return window_; }

private:
  static constexpr std::uint64_t B = 0x9e3779b97f4a7c15ULL;
  std::size_t window_;
  std::vector<unsigned char> buffer_;
  std::vector<std::uint64_t> pw_;  // B^i mod 2^64
  std::uint64_t h_ = 0;
  std::size_t in_ = 0;
};

// Find the next content-defined boundary at or after start+min_chunk and at or
// before start+max_chunk. Returns data.size() when no boundary is found in the
// allowed window (the caller should clamp to max_chunk).
std::size_t next_boundary(ConstBytes data, std::size_t start,
                          std::size_t min_chunk, std::size_t target_chunk,
                          std::size_t max_chunk);

// Split data into content-defined chunks. offsets[0] == 0 and the final chunk
// ends at data.size(). Chunk sizes respect [min_chunk, max_chunk].
std::vector<std::size_t> split(ConstBytes data, std::size_t min_chunk = 1u << 16,
                               std::size_t target_chunk = 1u << 20,
                               std::size_t max_chunk = 1u << 26);

} // namespace zpaq_ng::chunking

#endif // ZPAQ_NG_CHUNKING_CHUNKING_HPP