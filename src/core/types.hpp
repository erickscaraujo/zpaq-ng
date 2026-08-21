// types.hpp - Fundamental types for ZPAQ Next Generation.
//
// ZPAQ Next Generation (ZPAQ-NG) - Copyright (c) 2026 Erick de S.C. Araujo.
// This project is a modernization of the ZPAQ archiver. The original ZPAQ
// reference implementation is public domain (Matt Mahoney). This file is
// distributed under the Unlicense (see LICENSE).
//
// The compression core must remain numerically compatible with the ZPAQ
// level 2 format. Fixed-width integer types are used throughout so that
// archive compatibility never depends on the target platform's native
// integer widths.

#ifndef ZPAQ_NG_CORE_TYPES_HPP
#define ZPAQ_NG_CORE_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <string_view>

namespace zpaq_ng {

// Fixed width integer aliases (matching libzpaq's U8/U16/U32/U64).
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Signed 8/16/32/64 bit integers.
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// A single byte of input/output data.
using byte = u8;

// A mutable span over bytes.
using Bytes = std::span<byte>;

// A read-only span over bytes.
using ConstBytes = std::span<const byte>;

// Convenience read-only byte view from a std::string_view.
inline ConstBytes as_bytes(std::string_view s) noexcept {
  return {reinterpret_cast<const byte*>(s.data()), s.size()};
}

// Convenience read-only byte view from a pointer + size.
template <typename T>
inline ConstBytes as_bytes(const T* p, std::size_t n) noexcept {
  return {reinterpret_cast<const byte*>(p), n};
}

// Bytes used to count things. The ZPAQ format caps block sizes at 2^31-4096
// bytes and index entries at 2^32-1, so 64 bits is always sufficient.
using Offset = std::uint64_t;

} // namespace zpaq_ng

#endif // ZPAQ_NG_CORE_TYPES_HPP