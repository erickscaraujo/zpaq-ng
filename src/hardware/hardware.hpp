// hardware.hpp - Hardware abstraction for ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Detects the CPU (cores, threads, SIMD features), RAM size, and reports the
// device set available to the scheduler. GPU/XPU detection is intentionally
// conservative: v1.0 has no GPU backend, so this layer reports CPU-only unless
// a future backend is compiled in. The absence of a GPU is never an error.

#ifndef ZPAQ_NG_HARDWARE_HARDWARE_HPP
#define ZPAQ_NG_HARDWARE_HARDWARE_HPP

#include <cstdint>
#include <string>

namespace zpaq_ng::hardware {

// CPU SIMD feature levels, ordered from least to most capable.
enum class SimdLevel {
  NONE = 0,   // scalar fallback
  SSE2 = 1,
  SSSE3 = 2,
  SSE41 = 3,
  SSE42 = 4,
  AVX = 5,
  AVX2 = 6,
  AVX512F = 7,
  NEON = 8,   // ARM
  SVE = 9,    // ARM scalable vector extensions
};

// A snapshot of the host hardware.
struct HardwareInfo {
  unsigned core_count = 1;       // physical/logical processors usable
  unsigned thread_count = 1;     // logical processors
  std::uint64_t ram_bytes = 0;   // physical RAM, 0 if unknown
  SimdLevel simd = SimdLevel::NONE;
  bool has_avx512f = false;
  bool has_bmi2 = false;
  bool has_neon = false;
  std::string arch;              // e.g. "x86-64", "aarch64"
  std::string cpu_brand;         // e.g. "AMD Ryzen 9 ..."
  std::string os;                // e.g. "Windows", "Linux"
  std::string compiler;          // e.g. "GCC 14.2.0"

  bool gpu_available = false;    // true only when a backend is compiled in
  bool xpu_available = false;    // true only when a backend is compiled in
  std::string gpu_backend;       // e.g. "none (CPU-only build)"
  std::string xpu_backend;       // e.g. "none"
};

// Return a fresh snapshot of the host.
HardwareInfo detect();

// The best SIMD level supported by both the CPU and this build.
SimdLevel best_simd(const HardwareInfo& h) noexcept;

// A short human readable name for a SIMD level.
const char* simd_name(SimdLevel s) noexcept;

// A JSON object describing the device set (used by `zpaq_ng devices --json`).
std::string to_json(const HardwareInfo& h);

// Print a human readable report of the hardware (used by `zpaq_ng devices`).
std::string report(const HardwareInfo& h);

} // namespace zpaq_ng::hardware

#endif // ZPAQ_NG_HARDWARE_HARDWARE_HPP