// sha1.cpp - Incremental SHA-1 hash (FIPS 180-1).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Register rotation is expressed as an explicit round-robin of five 32-bit
// words, matching the reference libzpaq implementation step for step.

#include "integrity/sha1.hpp"

#include <algorithm>
#include <cstddef>

namespace zpaq_ng::integrity {

namespace {
inline u32 rotl(u32 x, int n) noexcept {
  return (x << n) | (x >> (32 - n));
}
} // namespace

void SHA1::init() noexcept {
  len = 0;
  h[0] = 0x67452301;
  h[1] = 0xEFCDAB89;
  h[2] = 0x98BADCFE;
  h[3] = 0x10325476;
  h[4] = 0xC3D2E1F0;
  std::memset(w, 0, sizeof(w));
}

void SHA1::write(const char* buf, std::size_t n) {
  const auto* p = reinterpret_cast<const unsigned char*>(buf);
  while (n > 0 && (len & 511) != 0) {
    put(*p++);
    --n;
  }
  while (n >= 64) {
    for (int i = 0; i < 16; ++i) {
      w[i] = static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 |
             static_cast<u32>(p[2]) << 8 | static_cast<u32>(p[3]);
      p += 4;
    }
    len += 512;
    process();
    n -= 64;
  }
  while (n > 0) {
    put(*p++);
    --n;
  }
}

void SHA1::process() noexcept {
  static constexpr u32 k[4] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};

  // Roles (x0,x1,x2,x3,x4). Each step: x4 += rotl(x0,5)+k+w+fmix(x1,x2,x3)
  // and x1 = rotl(x1,30). Then roles rotate to (x4,x0,x1,x2,x3).
  u32 r[5] = {h[0], h[1], h[2], h[3], h[4]};
  for (int i = 0; i < 80; ++i) {
    if (i >= 16) {
      w[i & 15] ^= w[(i - 3) & 15] ^ w[(i - 8) & 15] ^ w[(i - 14) & 15];
      w[i & 15] = rotl(w[i & 15], 1);
    }
    u32 fmix;
    if (i % 40 >= 20) {
      fmix = r[1] ^ r[2] ^ r[3];
    } else if (i >= 40) {
      fmix = (r[1] & r[2]) | (r[3] & (r[1] | r[2]));
    } else {
      fmix = r[3] ^ (r[1] & (r[2] ^ r[3]));
    }
    r[4] += rotl(r[0], 5) + k[i / 20] + w[i & 15] + fmix;
    r[1] = rotl(r[1], 30);
    std::rotate(r, r + 4, r + 5);
  }
  // 80 rotations restore the original role order.
  h[0] += r[0];
  h[1] += r[1];
  h[2] += r[2];
  h[3] += r[3];
  h[4] += r[4];
}

const char* SHA1::result() {
  const u64 s = len;
  put(0x80);
  while ((len & 511) != 448) put(0);
  put(static_cast<int>(s >> 56));
  put(static_cast<int>(s >> 48));
  put(static_cast<int>(s >> 40));
  put(static_cast<int>(s >> 32));
  put(static_cast<int>(s >> 24));
  put(static_cast<int>(s >> 16));
  put(static_cast<int>(s >> 8));
  put(static_cast<int>(s));
  for (int i = 0; i < 5; ++i) {
    hbuf[4 * i] = static_cast<char>(h[i] >> 24);
    hbuf[4 * i + 1] = static_cast<char>(h[i] >> 16);
    hbuf[4 * i + 2] = static_cast<char>(h[i] >> 8);
    hbuf[4 * i + 3] = static_cast<char>(h[i]);
  }
  init();
  return hbuf;
}

} // namespace zpaq_ng::integrity