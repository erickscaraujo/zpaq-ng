// hardware.cpp - Hardware detection implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "hardware/hardware.hpp"

#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

#include <cstring>

namespace zpaq_ng::hardware {

namespace {

#if defined(__GNUC__) || defined(__clang__)
struct CpuidResult {
  unsigned eax, ebx, ecx, edx;
};

CpuidResult cpuid(unsigned leaf, unsigned subleaf = 0) noexcept {
  unsigned a = 0, b = 0, c = 0, d = 0;
  __cpuid_count(leaf, subleaf, a, b, c, d);
  return {a, b, c, d};
}
#endif

// Query the CPU brand string (12 registers worth of ASCII).
std::string cpu_brand_string() {
#if defined(__GNUC__) || defined(__clang__)
  CpuidResult rmax = cpuid(0x80000000);
  if (rmax.eax < 0x80000004) return {};
  char buf[49];
  for (unsigned i = 0; i < 3; ++i) {
    CpuidResult x = cpuid(0x80000002 + i);
    std::memcpy(buf + i * 16, &x.eax, 4);
    std::memcpy(buf + i * 16 + 4, &x.ebx, 4);
    std::memcpy(buf + i * 16 + 8, &x.ecx, 4);
    std::memcpy(buf + i * 16 + 12, &x.edx, 4);
  }
  buf[48] = 0;
  std::string s(buf);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
#else
  return {};
#endif
}

// Query x86 CPUID feature flags.
SimdLevel detect_x86_simd() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(__GNUC__) || defined(__clang__)
  CpuidResult r = cpuid(1);
  const unsigned ecx = r.ecx;
  const unsigned edx = r.edx;
  SimdLevel level = SimdLevel::NONE;
  if (edx & (1u << 26)) level = SimdLevel::SSE2;      // SSE2
  if (ecx & (1u << 9)) level = SimdLevel::SSSE3;      // SSSE3
  if (ecx & (1u << 19)) level = SimdLevel::SSE41;     // SSE4.1
  if (ecx & (1u << 20)) level = SimdLevel::SSE42;     // SSE4.2
  // AVX and OSXSAVE (XSAVE enabled by OS)
  const bool osxsave = (ecx & (1u << 27)) != 0;
  const bool avx = (ecx & (1u << 28)) != 0;
  if (avx && osxsave) {
    level = SimdLevel::AVX;
    // XCR0 check: OS must enable SSE+AVX state.
    unsigned xcr0 = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0) : "c"(0) : "edx");
    const bool os_avx = (xcr0 & 0x6) == 0x6;  // XMM + YMM
    if (os_avx) {
      // AVX2 (leaf 7)
      CpuidResult r7 = cpuid(7, 0);
      const unsigned ebx = r7.ebx;
      const bool avx2 = (ebx & (1u << 5)) != 0;
      const bool avx512f = (ebx & (1u << 16)) != 0;
      const bool avx512os = (xcr0 & 0xE6) == 0xE6;  // XMM+YMM+ZMM+opmask
      if (avx2) level = SimdLevel::AVX2;
      if (avx512f && avx512os) {
        level = SimdLevel::AVX512F;
        return level;  // AVX512F implies AVX2 support
      }
    }
  }
  return level;
#else
  // MSVC: not compiled on this toolchain; scalar fallback is always safe.
  return SimdLevel::NONE;
#endif
#else
  return SimdLevel::NONE;
#endif
}

} // namespace

SimdLevel best_simd(const HardwareInfo& h) noexcept {
  // At build time we only compile x86 kernels. NEON is detected at runtime
  // but the scalar fallback is used unless a NEON kernel is compiled.
  if (h.simd >= SimdLevel::AVX512F && h.has_avx512f) return SimdLevel::AVX512F;
  if (h.simd >= SimdLevel::AVX2) return SimdLevel::AVX2;
  if (h.simd >= SimdLevel::SSE2) return SimdLevel::SSE2;
  return SimdLevel::NONE;
}

const char* simd_name(SimdLevel s) noexcept {
  switch (s) {
    case SimdLevel::NONE: return "Scalar";
    case SimdLevel::SSE2: return "SSE2";
    case SimdLevel::SSSE3: return "SSSE3";
    case SimdLevel::SSE41: return "SSE4.1";
    case SimdLevel::SSE42: return "SSE4.2";
    case SimdLevel::AVX: return "AVX";
    case SimdLevel::AVX2: return "AVX2";
    case SimdLevel::AVX512F: return "AVX-512F";
    case SimdLevel::NEON: return "NEON";
    case SimdLevel::SVE: return "SVE";
  }
  return "Unknown";
}

HardwareInfo detect() {
  HardwareInfo h;

  // Threads and cores.
  unsigned hw = std::thread::hardware_concurrency();
  h.thread_count = hw > 0 ? hw : 1;
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  h.core_count = si.dwNumberOfProcessors > 0
                     ? static_cast<unsigned>(si.dwNumberOfProcessors)
                     : h.thread_count;
  h.os = "Windows";
#else
  h.core_count = h.thread_count;
  h.os = "Unix";
#endif

  // RAM.
#if defined(_WIN32)
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) h.ram_bytes = ms.ullTotalPhys;
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long psize = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && psize > 0)
    h.ram_bytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(psize);
#endif

  // SIMD and architecture.
#if defined(__x86_64__) || defined(_M_X64)
  h.arch = "x86-64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  h.arch = "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
  h.arch = "x86-32";
#else
  h.arch = "unknown";
#endif

  h.simd = detect_x86_simd();
#if defined(__x86_64__) || defined(_M_X64)
  // Query AVX512F / BMI2 leaf 7 flags when the CPUID path is available.
#if defined(__GNUC__) || defined(__clang__)
  CpuidResult r7 = cpuid(7, 0);
  h.has_avx512f = (r7.ebx & (1u << 16)) != 0;
  h.has_bmi2 = (r7.ebx & (1u << 8)) != 0;
#endif
#else
  h.has_avx512f = false;
#endif

#if defined(__aarch64__)
  h.has_neon = true;
  if (h.simd == SimdLevel::NONE) h.simd = SimdLevel::NEON;
#endif

  h.cpu_brand = cpu_brand_string();
#if defined(__GNUC__) || defined(__clang__)
#if defined(__GNUC__)
  h.compiler = "GCC " __VERSION__;
#else
  h.compiler = "Clang";
#endif
#elif defined(_MSC_VER)
  h.compiler = "MSVC";
#else
  h.compiler = "unknown";
#endif

  // v1.0 is CPU-only. No fake GPU/XPU backends are advertised.
  h.gpu_available = false;
  h.xpu_available = false;
  h.gpu_backend = "none (CPU-only build)";
  h.xpu_backend = "none";

  return h;
}

std::string to_json(const HardwareInfo& h) {
  std::string s;
  s += "{\n";
  s += "  \"cpu\": {\n";
  s += "    \"arch\": \"" + h.arch + "\",\n";
  s += "    \"brand\": \"" + h.cpu_brand + "\",\n";
  s += "    \"cores\": " + std::to_string(h.core_count) + ",\n";
  s += "    \"threads\": " + std::to_string(h.thread_count) + ",\n";
  s += "    \"simd\": \"" + std::string(simd_name(h.simd)) + "\"\n";
  s += "  },\n";
  s += "  \"ram_bytes\": " + std::to_string(h.ram_bytes) + ",\n";
  s += "  \"gpu\": {\"available\": false, \"backend\": \"" + h.gpu_backend + "\"},\n";
  s += "  \"xpu\": {\"available\": false, \"backend\": \"" + h.xpu_backend + "\"}\n";
  s += "}";
  return s;
}

std::string report(const HardwareInfo& h) {
  std::string s;
  s += "ZPAQ-NG Hardware\n";
  s += "---------------\n";
  s += "CPU:\n";
  s += "  Arch: " + h.arch + "\n";
  s += "  Brand: " + (h.cpu_brand.empty() ? std::string("(unknown)") : h.cpu_brand) + "\n";
  s += "  Cores: " + std::to_string(h.core_count) + "\n";
  s += "  Threads: " + std::to_string(h.thread_count) + "\n";
  s += "  SIMD: " + std::string(simd_name(h.simd)) + "\n";
  s += "  AVX-512F: " + std::string(h.has_avx512f ? "yes" : "no") + "\n";
  s += "  BMI2: " + std::string(h.has_bmi2 ? "yes" : "no") + "\n";
  s += "RAM: " + std::to_string(h.ram_bytes / (1024 * 1024)) + " MB\n";
  s += "GPU:\n";
  s += "  Available: " + std::string(h.gpu_available ? "Yes" : "No") + " (" + h.gpu_backend + ")\n";
  s += "XPU:\n";
  s += "  Available: " + std::string(h.xpu_available ? "Yes" : "No") + " (" + h.xpu_backend + ")\n";
  s += "\nRecommended:\n";
  s += "  CPU + " + std::string(simd_name(best_simd(h))) + "\n";
  s += "  Threads: " + std::to_string(h.thread_count) + "\n";
  return s;
}

} // namespace zpaq_ng::hardware