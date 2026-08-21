// sha1.hpp - Incremental SHA-1 hash.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// SHA-1 is the checksum used by the ZPAQ format (20 byte segment hashes),
// so this implementation must remain bit-compatible with the reference.
// Implemented from the FIPS 180-1 specification with standard test vectors.

#ifndef ZPAQ_NG_INTEGRITY_SHA1_HPP
#define ZPAQ_NG_INTEGRITY_SHA1_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/types.hpp"

namespace zpaq_ng::integrity {

class SHA1 {
public:
  SHA1() { init(); }

  // Hash one byte.
  void put(int c) {
    u32& r = w[(len >> 5) & 15];
    r = (r << 8) | (static_cast<u32>(c) & 255);
    len += 8;
    if ((len & 511) == 0) process();
  }

  // Hash buf[0..n-1].
  void write(const char* buf, std::size_t n);

  // Total number of bytes hashed.
  std::uint64_t usize() const noexcept { return len / 8; }
  std::size_t size() const noexcept { return static_cast<std::size_t>(len / 8); }

  // Return the 20 byte digest (pointer valid until the next call) and
  // reset the hash state for a new computation.
  const char* result();

  // Compare two digests (20 bytes each) for equality.
  static bool equal(const char* a, const char* b) noexcept {
    return std::memcmp(a, b, 20) == 0;
  }

private:
  void init() noexcept;
  void process() noexcept;

  u64 len = 0;      // length in bits
  u32 h[5];         // hash state
  u32 w[16];        // input block buffer
  char hbuf[20];    // digest buffer
};

} // namespace zpaq_ng::integrity

#endif // ZPAQ_NG_INTEGRITY_SHA1_HPP