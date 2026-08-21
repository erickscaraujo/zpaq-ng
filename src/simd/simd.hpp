// simd.hpp - SIMD kernels with runtime CPU dispatch.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// A small set of data-parallel kernels used by the analyzer, chunker, and
// integrity paths. Kernels are selected once at startup from the detected CPU
// features; a scalar fallback is always available, so the program runs on any
// x86-64 CPU regardless of SIMD support. No AVX2/AVX-512 is ever required.

#ifndef ZPAQ_NG_SIMD_SIMD_HPP
#define ZPAQ_NG_SIMD_SIMD_HPP

#include <cstddef>
#include <cstdint>

#include "core/types.hpp"
#include "hardware/hardware.hpp"

namespace zpaq_ng::simd {

// Kernel table. Function pointers are filled by init() based on the detected
// CPU and only ever point to code the current CPU can execute.
struct Kernels {
  // Count byte values. hist must point to 256 zeroed uint32_t entries.
  void (*histogram)(const byte* p, std::size_t n, std::uint32_t* hist) = nullptr;
  // Sum of all byte values.
  std::uint64_t (*sum_bytes)(const byte* p, std::size_t n) = nullptr;
  // XOR of 64-bit words folded over the buffer (fast mixing/sketch).
  std::uint64_t (*fold_xor)(const byte* p, std::size_t n) = nullptr;
};

// Return the kernel table for the best detected SIMD level.
const Kernels& kernels() noexcept;

// Rebuild the kernel table using an explicit SIMD level (for tests).
void init(hardware::SimdLevel level);

// Explicit scalar kernels (public for testing/benchmarking).
void histogram_scalar(const byte* p, std::size_t n, std::uint32_t* hist);
std::uint64_t sum_bytes_scalar(const byte* p, std::size_t n);
std::uint64_t fold_xor_scalar(const byte* p, std::size_t n);

} // namespace zpaq_ng::simd

#endif // ZPAQ_NG_SIMD_SIMD_HPP