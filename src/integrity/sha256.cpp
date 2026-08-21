// sha256.cpp - Incremental SHA-256 hash (FIPS 180-2).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "integrity/sha256.hpp"

#include <cstddef>

namespace zpaq_ng::integrity {

namespace {
inline u32 ror(u32 x, int n) noexcept { return (x >> n) | (x << (32 - n)); }
} // namespace

void SHA256::init() noexcept {
  len0 = len1 = 0;
  s[0] = 0x6a09e667;
  s[1] = 0xbb67ae85;
  s[2] = 0x3c6ef372;
  s[3] = 0xa54ff53a;
  s[4] = 0x510e527f;
  s[5] = 0x9b05688c;
  s[6] = 0x1f83d9ab;
  s[7] = 0x5be0cd19;
  std::memset(w, 0, sizeof(w));
}

void SHA256::write(const char* buf, std::size_t n) {
  const auto* p = reinterpret_cast<const unsigned char*>(buf);
  while (n > 0 && (len0 & 511) != 0) {
    put(*p++);
    --n;
  }
  while (n >= 64) {
    for (int i = 0; i < 16; ++i) {
      w[i] = static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 |
             static_cast<u32>(p[2]) << 8 | static_cast<u32>(p[3]);
      p += 4;
    }
    len0 += 512;
    if (len0 < 512) ++len1;
    process();
    n -= 64;
  }
  while (n > 0) {
    put(*p++);
    --n;
  }
}

void SHA256::process() noexcept {
  static constexpr u32 k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  u32 a = s[0], b = s[1], c = s[2], d = s[3];
  u32 e = s[4], f = s[5], g = s[6], h = s[7];

  for (int i = 0; i < 64; ++i) {
    if (i >= 16) {
      const u32 s0 = ror(w[(i - 15) & 15], 7) ^ ror(w[(i - 15) & 15], 18) ^
                     (w[(i - 15) & 15] >> 3);
      const u32 s1 = ror(w[(i - 2) & 15], 17) ^ ror(w[(i - 2) & 15], 19) ^
                     (w[(i - 2) & 15] >> 10);
      w[i & 15] += w[(i - 7) & 15] + s0 + s1;
    }
    const u32 S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
    const u32 ch = (e & f) ^ (~e & g);
    const u32 temp1 = h + S1 + ch + k[i] + w[i & 15];
    const u32 S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
    const u32 maj = (a & b) ^ (a & c) ^ (b & c);
    const u32 temp2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  s[0] += a;
  s[1] += b;
  s[2] += c;
  s[3] += d;
  s[4] += e;
  s[5] += f;
  s[6] += g;
  s[7] += h;
}

const char* SHA256::result() {
  const u32 s1 = len1, s0 = len0;
  put(0x80);
  while ((len0 & 511) != 448) put(0);
  put(static_cast<int>(s1 >> 24));
  put(static_cast<int>(s1 >> 16));
  put(static_cast<int>(s1 >> 8));
  put(static_cast<int>(s1));
  put(static_cast<int>(s0 >> 24));
  put(static_cast<int>(s0 >> 16));
  put(static_cast<int>(s0 >> 8));
  put(static_cast<int>(s0));
  for (int i = 0; i < 8; ++i) {
    hbuf[4 * i] = static_cast<char>(s[i] >> 24);
    hbuf[4 * i + 1] = static_cast<char>(s[i] >> 16);
    hbuf[4 * i + 2] = static_cast<char>(s[i] >> 8);
    hbuf[4 * i + 3] = static_cast<char>(s[i]);
  }
  init();
  return hbuf;
}

} // namespace zpaq_ng::integrity