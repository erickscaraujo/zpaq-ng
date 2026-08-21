// sha256.hpp - Incremental SHA-256 hash.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// SHA-256 is used for strong per-block and per-archive integrity and for the
// future encryption (scrypt key derivation) module. FIPS 180-2.

#ifndef ZPAQ_NG_INTEGRITY_SHA256_HPP
#define ZPAQ_NG_INTEGRITY_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/types.hpp"

namespace zpaq_ng::integrity {

class SHA256 {
public:
  SHA256() { init(); }

  void put(int c) {
    u32& r = w[(len0 >> 5) & 15];
    r = (r << 8) | (static_cast<u32>(c) & 255);
    len0 += 8;                       // length is counted in bits
    if (len0 == 0) ++len1;           // 32-bit low counter wrapped
    if ((len0 & 511) == 0) process();
  }

  void write(const char* buf, std::size_t n);

  std::uint64_t usize() const noexcept {
    return len0 / 8 + (static_cast<u64>(len1) << 29);
  }
  std::size_t size() const noexcept {
    return static_cast<std::size_t>(usize());
  }

  // Return the 32 byte digest and reset.
  const char* result();

private:
  void init() noexcept;
  void process() noexcept;

  u32 len0 = 0;   // length in bits (low)
  u32 len1 = 0;   // length in bits (high)
  u32 s[8];       // hash state
  u32 w[16];      // input block buffer
  char hbuf[32];  // digest buffer
};

} // namespace zpaq_ng::integrity

#endif // ZPAQ_NG_INTEGRITY_SHA256_HPP