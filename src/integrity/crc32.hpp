// crc32.hpp - Incremental CRC-32 (IEEE and Castagnoli) checksums.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// CRC-32C is used for fast chunk fingerprints in the deduplication pipeline
// (SHA-1 remains the authoritative identity used by the archive format).

#ifndef ZPAQ_NG_INTEGRITY_CRC32_HPP
#define ZPAQ_NG_INTEGRITY_CRC32_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace zpaq_ng::integrity {

// Standard IEEE CRC-32 (polynomial 0xEDB88320).
inline constexpr std::uint32_t CRC32_IEEE_POLY = 0xEDB88320u;

// Castagnoli CRC-32C (polynomial 0x82F63B78).
inline constexpr std::uint32_t CRC32_C_POLY = 0x82F63B78u;

// Incremental CRC-32 with the standard init 0xFFFFFFFF and final XOR.
class CRC32 {
public:
  explicit CRC32(std::uint32_t poly = CRC32_IEEE_POLY) noexcept : poly_(poly) {
    init();
  }

  void init() noexcept { crc_ = 0xFFFFFFFFu; }

  // Hash one byte.
  void put(int c) noexcept { crc_ = step(crc_, static_cast<unsigned char>(c)); }

  // Hash buf[0..n-1].
  void write(const char* buf, std::size_t n) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(buf);
    for (std::size_t i = 0; i < n; ++i) crc_ = step(crc_, p[i]);
  }

  // Return the checksum and reset for a new computation.
  std::uint32_t result() noexcept {
    const std::uint32_t r = crc_ ^ 0xFFFFFFFFu;
    init();
    return r;
  }

private:
  std::uint32_t step(std::uint32_t crc, unsigned char c) noexcept {
    return (crc >> 8) ^ table(crc, c);
  }

  std::uint32_t table(std::uint32_t crc, unsigned char c) noexcept;

  std::uint32_t poly_;
  std::uint32_t crc_ = 0;
};

} // namespace zpaq_ng::integrity

#endif // ZPAQ_NG_INTEGRITY_CRC32_HPP