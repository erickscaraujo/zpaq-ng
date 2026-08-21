// arithmetic.cpp - Binary arithmetic coder (ZPAQ-compatible).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Numeric rules match libzpaq's Encoder/Decoder exactly.

#include "entropy/arithmetic.hpp"

#include <cstring>

namespace zpaq_ng::entropy {

// ------------------------- Encoder -------------------------

void ArithmeticEncoder::init(bool store_mode) {
  low_ = 1;
  high_ = 0xFFFFFFFFu;
  if (store_mode) low_ = 0;
}

void ArithmeticEncoder::encode(int y, u32 p) {
  if (!out_) throw format_error("arithmetic encoder has no output");
  if (p >= 65536u) throw format_error("probability out of range");
  const u32 mid = low_ + static_cast<u32>((static_cast<u64>(high_ - low_) *
                                          static_cast<u64>(p)) >>
                                         16);
  if (y) high_ = mid;
  else low_ = mid + 1;
  while ((high_ ^ low_) < 0x1000000u) {  // write identical leading bytes
    out_->put(high_ >> 24);
    high_ = (high_ << 8) | 255u;
    low_ = low_ << 8;
    low_ += (low_ == 0);  // never code four 0 bytes in a row
  }
}

void ArithmeticEncoder::store_put(int c) {
  if (!out_) throw format_error("arithmetic encoder has no output");
  if (low_ && (c < 0 || low_ == sizeof buf_)) {
    out_->put((low_ >> 24) & 255);
    out_->put((low_ >> 16) & 255);
    out_->put((low_ >> 8) & 255);
    out_->put(low_ & 255);
    out_->write(reinterpret_cast<const char*>(buf_), low_);
    low_ = 0;
  }
  if (c >= 0) buf_[low_++] = static_cast<u8>(c);
}

void ArithmeticEncoder::store_flush() {
  if (low_) {
    out_->put((low_ >> 24) & 255);
    out_->put((low_ >> 16) & 255);
    out_->put((low_ >> 8) & 255);
    out_->put(low_ & 255);
    out_->write(reinterpret_cast<const char*>(buf_), low_);
    low_ = 0;
  }
}

// ------------------------- Decoder -------------------------

void ArithmeticDecoder::init(bool modeled) {
  low_ = 1;
  high_ = 0xFFFFFFFFu;
  curr_ = 0;
  (void)modeled;
}

int ArithmeticDecoder::decode(u32 p) {
  if (p >= 65536u) throw format_error("probability out of range");
  if (curr_ < low_ || curr_ > high_) throw format_error("archive corrupted");
  const u32 mid = low_ + static_cast<u32>((static_cast<u64>(high_ - low_) *
                                          static_cast<u64>(p)) >>
                                         16);
  int y;
  if (curr_ <= mid) {
    y = 1;
    high_ = mid;
  } else {
    y = 0;
    low_ = mid + 1;
  }
  while ((high_ ^ low_) < 0x1000000u) {  // shift out identical leading bytes
    high_ = (high_ << 8) | 255u;
    low_ = low_ << 8;
    low_ += (low_ == 0);
    const int c = get();
    if (c < 0) throw format_error("unexpected end of file");
    curr_ = (curr_ << 8) | static_cast<u32>(c);
  }
  return y;
}

int ArithmeticDecoder::store_get() {
  if (curr_ == 0) {
    // Segment initialization: read the 4 byte big-endian length.
    curr_ = 0;
    for (int i = 0; i < 4; ++i) curr_ = (curr_ << 8) | static_cast<u32>(get());
    if (curr_ == 0) return -1;
  }
  --curr_;
  return get();
}

int ArithmeticDecoder::skip(bool modeled) {
  int c = -1;
  if (modeled) {
    while (curr_ == 0)  // at start?
      curr_ = static_cast<u32>(get());
    while (curr_ && (c = get()) >= 0)  // find 4 zeros
      curr_ = (curr_ << 8) | static_cast<u32>(c);
    while ((c = get()) == 0)
      ;  // might be more than 4
    return c;
  } else {
    if (curr_ == 0)  // at start?
      for (int i = 0; i < 4 && (c = get()) >= 0; ++i)
        curr_ = (curr_ << 8) | static_cast<u32>(c);
    while (curr_ > 0) {
      while (curr_ > 0) {
        --curr_;
        if (get() < 0) throw format_error("skipped to EOF");
      }
      for (int i = 0; i < 4 && (c = get()) >= 0; ++i)
        curr_ = (curr_ << 8) | static_cast<u32>(c);
    }
    if (c >= 0) c = get();
    return c;
  }
}

int ArithmeticDecoder::get() {
  if (rpos_ == wpos_) {
    rpos_ = 0;
    wpos_ = in_ ? static_cast<u32>(in_->read(buf_, sizeof buf_)) : 0;
  }
  return rpos_ < wpos_ ? static_cast<unsigned char>(buf_[rpos_++]) : -1;
}

std::size_t ArithmeticDecoder::read(char* out, std::size_t n) {
  std::size_t total = 0;
  while (total < n) {
    const int c = get();
    if (c < 0) break;
    out[total++] = static_cast<char>(c);
  }
  return total;
}

} // namespace zpaq_ng::entropy