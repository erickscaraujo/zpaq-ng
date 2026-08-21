// simd.cpp - SIMD kernel implementations and runtime dispatch.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Kernels are selected at runtime from the detected CPU. The scalar fallback
// is always correct; SSE2/AVX2 variants are compiled for x86-64 via target
// attributes so a single translation unit serves every CPU.

#include "simd/simd.hpp"

#include <cstring>
#include <mutex>

namespace zpaq_ng::simd {

// ---- Scalar reference implementations -----------------------------------

void histogram_scalar(const byte* p, std::size_t n, std::uint32_t* hist) {
  for (std::size_t i = 0; i < n; ++i) ++hist[p[i]];
}

std::uint64_t sum_bytes_scalar(const byte* p, std::size_t n) {
  std::uint64_t s = 0;
  for (std::size_t i = 0; i < n; ++i) s += p[i];
  return s;
}

std::uint64_t fold_xor_scalar(const byte* p, std::size_t n) {
  std::uint64_t x = 0;
  std::size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    std::uint64_t w;
    std::memcpy(&w, p + i, 8);
    x ^= w;
  }
  for (; i < n; ++i) x ^= static_cast<std::uint64_t>(p[i]) << (8 * (i & 7));
  return x;
}

// ---- SIMD variants (x86-64 only, guarded by target attributes) ----------

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))

#include <immintrin.h>

#define ZPAQ_NG_SIMD_AVX2 1

namespace {

__attribute__((target("sse2")))
std::uint64_t sum_bytes_sse2(const byte* p, std::size_t n) {
  const __m128i zero = _mm_setzero_si128();
  __m128i acc = zero;
  std::size_t i = 0;
  for (; i + 16 <= n; i += 16) {
    __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i));
    acc = _mm_add_epi64(acc, _mm_sad_epu8(zero, v));
  }
  std::uint64_t s = static_cast<std::uint64_t>(acc[0]) +
                    static_cast<std::uint64_t>(acc[1]);
  for (; i < n; ++i) s += p[i];
  return s;
}

__attribute__((target("avx2")))
std::uint64_t sum_bytes_avx2(const byte* p, std::size_t n) {
  const __m256i zero = _mm256_setzero_si256();
  __m256i acc = zero;
  std::size_t i = 0;
  for (; i + 32 <= n; i += 32) {
    __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
    acc = _mm256_add_epi64(acc, _mm256_sad_epu8(zero, v));
  }
  std::uint64_t s = 0;
  for (int k = 0; k < 4; ++k) s += static_cast<std::uint64_t>(acc[k]);
  for (; i < n; ++i) s += p[i];
  return s;
}

__attribute__((target("avx2")))
std::uint64_t fold_xor_avx2(const byte* p, std::size_t n) {
  const __m256i zero = _mm256_setzero_si256();
  __m256i acc = zero;
  std::size_t i = 0;
  for (; i + 32 <= n; i += 32) {
    __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
    acc = _mm256_xor_si256(acc, v);
  }
  std::uint64_t x = 0;
  for (int k = 0; k < 4; ++k) x ^= static_cast<std::uint64_t>(acc[k]);
  for (; i < n; ++i) x ^= static_cast<std::uint64_t>(p[i]) << (8 * (i & 7));
  return x;
}

}  // namespace

#endif  // x86-64

// ---- Dispatch -------------------------------------------------------------

namespace {

Kernels g_kernels;
std::once_flag g_once;

Kernels make_scalar() {
  Kernels k;
  k.histogram = &histogram_scalar;
  k.sum_bytes = &sum_bytes_scalar;
  k.fold_xor = &fold_xor_scalar;
  return k;
}

void configure() {
  g_kernels = make_scalar();
#if defined(ZPAQ_NG_SIMD_AVX2)
  const hardware::SimdLevel level =
      hardware::best_simd(hardware::detect());
  if (level >= hardware::SimdLevel::AVX2) {
    g_kernels.sum_bytes = &sum_bytes_avx2;
    g_kernels.fold_xor = &fold_xor_avx2;
  } else if (level >= hardware::SimdLevel::SSE2) {
    g_kernels.sum_bytes = &sum_bytes_sse2;
  }
#endif
}

}  // namespace

void init(hardware::SimdLevel level) {
  static std::mutex mtx;
  std::lock_guard<std::mutex> lock(mtx);
  g_kernels = make_scalar();
#if defined(ZPAQ_NG_SIMD_AVX2)
  if (level >= hardware::SimdLevel::AVX2) {
    g_kernels.sum_bytes = &sum_bytes_avx2;
    g_kernels.fold_xor = &fold_xor_avx2;
  } else if (level >= hardware::SimdLevel::SSE2) {
    g_kernels.sum_bytes = &sum_bytes_sse2;
  }
#endif
}

const Kernels& kernels() noexcept {
  std::call_once(g_once, configure);
  return g_kernels;
}

}  // namespace zpaq_ng::simd