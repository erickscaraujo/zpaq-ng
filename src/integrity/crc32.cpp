// crc32.cpp - Incremental CRC-32 (IEEE and Castagnoli) checksums.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Standard table driven implementation. Tables are generated lazily once per
// polynomial (negligible cost) to keep the source self-contained.

#include "integrity/crc32.hpp"

#include <cstdint>

namespace zpaq_ng::integrity {

namespace {

std::array<std::uint32_t, 256> make_table(std::uint32_t poly) noexcept {
  std::array<std::uint32_t, 256> t{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t crc = i;
    for (int j = 0; j < 8; ++j) {
      crc = (crc & 1) ? (crc >> 1) ^ poly : crc >> 1;
    }
    t[i] = crc;
  }
  return t;
}

const std::array<std::uint32_t, 256>& table_for(std::uint32_t poly) noexcept {
  static const auto ieee = make_table(CRC32_IEEE_POLY);
  static const auto castagnoli = make_table(CRC32_C_POLY);
  return poly == CRC32_C_POLY ? castagnoli : ieee;
}

} // namespace

std::uint32_t CRC32::table(std::uint32_t crc, unsigned char c) noexcept {
  return table_for(poly_)[(crc ^ c) & 0xFF];
}

} // namespace zpaq_ng::integrity