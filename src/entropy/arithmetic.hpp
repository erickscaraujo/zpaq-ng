// arithmetic.hpp - Binary arithmetic coder (ZPAQ-compatible).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// This is a faithful, modernized port of the arithmetic coder from the ZPAQ
// reference implementation. The numeric behavior is part of the ZPAQ level 2
// format: archives produced by the original zpaq must decode correctly and
// vice versa, so the range update rules and the byte normalization loops are
// preserved exactly.
//
// The coder operates on individual bits with a 16 bit probability p of the
// next bit being 1. Byte output/input is handled by io::Writer / io::Reader.
//
// Two modes exist, mirroring libzpaq:
//   * modeled  : binary range coding driven by a context-mixing predictor.
//   * store    : no model; a 4 byte big-endian length followed by raw bytes.

#ifndef ZPAQ_NG_ENTROPY_ARITHMETIC_HPP
#define ZPAQ_NG_ENTROPY_ARITHMETIC_HPP

#include <cstdint>

#include "core/error.hpp"
#include "core/types.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::entropy {

// Encoder side of the range coder.
//
// State conventions (identical to libzpaq): low, high form the current
// range with 1 <= low < high <= 0xFFFFFFFF, and every normalization writes
// one leading byte.
class ArithmeticEncoder {
public:
  explicit ArithmeticEncoder(io::Writer* out = nullptr) : out_(out) {}

  void set_output(io::Writer* out) noexcept { out_ = out; }

  io::Writer* output() const noexcept { return out_; }

  // Initialize at the start of a block. For store mode the byte counter is
  // reset; for modeled mode the range is reset to its initial width.
  void init(bool store_mode);

  // Encode bit y (0 or 1) with 16 bit probability p of being 1.
  void encode(int y, u32 p);

  // Store-mode byte writer: c is 0..255, or -1 at end of stream. Flushes a
  // 4 byte big-endian length followed by the buffered bytes as needed.
  void store_put(int c);

  // Flush any buffered store-mode bytes (used at end of segment).
  void store_flush();

private:
  io::Writer* out_ = nullptr;
  u32 low_ = 1;
  u32 high_ = 0xFFFFFFFFu;
  // Store mode reuses low_ as the buffered byte count.
  u8 buf_[1 << 16];
};

// Decoder side of the range coder. Implements io::Reader so that it can be
// chained as the input of a decompressor; buffered reads reduce the number
// of virtual calls per byte.
class ArithmeticDecoder : public io::Reader {
public:
  explicit ArithmeticDecoder(io::Reader* in = nullptr) : in_(in) {}

  using io::Reader::skip;

  void set_input(io::Reader* in) noexcept { in_ = in; }

  // Initialize at the start of a block.
  void init(bool modeled);

  // Return the next decoded bit (0..1) with 16 bit probability p of 1.
  int decode(u32 p);

  // Store-mode byte reader: returns the next raw byte, or -1 at end of
  // segment. curr_ holds the remaining store byte count.
  int store_get();

  // Skip to the end of the current segment and return the next byte (the
  // one following the end marker). modeled selects the modeled (range coded)
  // vs. store (length prefixed) format.
  int skip(bool modeled);

  // ---- io::Reader interface (buffered input reads) ----
  int get() override;

  std::size_t read(char* out, std::size_t n) override;

  // Number of bytes currently buffered and not yet consumed.
  int buffered() const noexcept { return static_cast<int>(wpos_ - rpos_); }

  u32 curr_state() const noexcept { return curr_; }
  u32 low_state() const noexcept { return low_; }
  u32 high_state() const noexcept { return high_; }
  void set_curr(u32 v) noexcept { curr_ = v; }

private:
  static constexpr std::size_t BUFSIZE = 1 << 16;
  io::Reader* in_ = nullptr;
  u32 low_ = 1;
  u32 high_ = 0xFFFFFFFFu;
  u32 curr_ = 0;   // range code value (modeled) or store byte count
  u32 rpos_ = 0;
  u32 wpos_ = 0;
  char buf_[BUFSIZE];
};

} // namespace zpaq_ng::entropy

#endif // ZPAQ_NG_ENTROPY_ARITHMETIC_HPP