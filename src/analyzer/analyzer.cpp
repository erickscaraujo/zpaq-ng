// analyzer.cpp - Content analyzer and classification implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "analyzer/analyzer.hpp"

#include <cmath>
#include <cstring>

#include "simd/simd.hpp"

namespace zpaq_ng::analyzer {

const char* class_name(ContentClass c) noexcept {
  switch (c) {
    case ContentClass::TEXT: return "TEXT";
    case ContentClass::SOURCE_CODE: return "SOURCE_CODE";
    case ContentClass::JSON: return "JSON";
    case ContentClass::XML: return "XML";
    case ContentClass::CSV: return "CSV";
    case ContentClass::LOG: return "LOG";
    case ContentClass::DATABASE: return "DATABASE";
    case ContentClass::BINARY: return "BINARY";
    case ContentClass::EXECUTABLE: return "EXECUTABLE";
    case ContentClass::SCIENTIFIC: return "SCIENTIFIC";
    case ContentClass::REPETITIVE: return "REPETITIVE";
    case ContentClass::RANDOM: return "RANDOM";
    case ContentClass::MIXED: return "MIXED";
    case ContentClass::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

namespace {

inline bool is_printable(unsigned char c) {
  return (c >= 0x20 && c <= 0x7e) || c == '\t' || c == '\n' || c == '\r';
}

// Detect a known file signature at the head of the sample.
std::string detect_magic(ConstBytes s) {
  static const struct {
    const char* name;
    const char* sig;
    std::size_t len;
  } magics[] = {
      {"PE", "\x4d\x5a", 2},
      {"ELF", "\x7f""ELF", 4},
      {"gzip", "\x1f\x8b", 2},
      {"zlib", "\x78\x9c", 2},
      {"png", "\x89PNG\r\n\x1a\n", 8},
      {"jpg", "\xff\xd8\xff", 3},
      {"pdf", "%PDF", 4},
      {"zip", "PK\x03\x04", 4},
      {"7z", "7z\xbc\xaf\x27\x1c", 6},
      {"tar", "ustar", 5},
      {"lzma", "\x5d\x00\x00", 3},
  };
  for (const auto& m : magics) {
    if (s.size() >= m.len && std::memcmp(s.data(), m.sig, m.len) == 0)
      return m.name;
  }
  return {};
}

} // namespace

Analysis analyze(ConstBytes sample, std::size_t max_sample) {
  Analysis a;
  const std::size_t n = std::min(sample.size(), max_sample);
  a.sample_size = n;
  if (n == 0) {
    a.cls = ContentClass::UNKNOWN;
    return a;
  }

  const byte* p = sample.data();
  a.hist.fill(0);
  simd::kernels().histogram(p, n, a.hist.data());

  // Distinct bytes and entropy.
  double sum = 0.0;
  a.distinct = 0;
  for (unsigned i = 0; i < 256; ++i) {
    if (a.hist[i]) {
      ++a.distinct;
      const double pr = static_cast<double>(a.hist[i]) / static_cast<double>(n);
      sum -= pr * std::log2(pr);
    }
  }
  a.entropy = sum;

  // Top-8 mass.
  std::array<std::uint32_t, 256> h = a.hist;
  std::uint64_t top8 = 0;
  for (int k = 0; k < 8; ++k) {
    std::uint32_t best = 0;
    unsigned bi = 0;
    for (unsigned i = 0; i < 256; ++i)
      if (h[i] > best) { best = h[i]; bi = i; }
    if (best == 0) break;
    top8 += best;
    h[bi] = 0;
  }
  a.top8_fraction = static_cast<double>(top8) / static_cast<double>(n);

  // Printable, zeros, runs, newlines.
  std::uint64_t printable = 0, zeros = 0, runs = 0, newlines = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (is_printable(p[i])) ++printable;
    if (p[i] == 0) ++zeros;
    if (i > 0 && p[i] == p[i - 1]) ++runs;
    if (p[i] == '\n') ++newlines;
  }
  a.printable_fraction = static_cast<double>(printable) / static_cast<double>(n);
  a.zero_fraction = static_cast<double>(zeros) / static_cast<double>(n);
  a.run_fraction = static_cast<double>(runs) / static_cast<double>(n);
  a.newline_density =
      static_cast<double>(newlines) / (static_cast<double>(n) / 1024.0);

  a.subtype = detect_magic(sample);
  classify(a);
  return a;
}

void classify(Analysis& a) {
  const double H = a.entropy;
  const double printable = a.printable_fraction;
  const double runs = a.run_fraction;
  const double zeros = a.zero_fraction;
  const std::size_t n = a.sample_size;

  // Known binary/archive signatures (set by analyze via detect_magic).
  static const char* exec_sigs[] = {"PE", "ELF", "gzip", "zlib", "png",
                                    "jpg", "pdf", "zip", "7z", "tar", "lzma"};
  for (const char* s : exec_sigs) {
    if (a.subtype == s) {
      a.cls = ContentClass::EXECUTABLE;
      return;
    }
  }

  // Near-random data: essentially incompressible.
  if (H >= 7.85 && a.distinct >= 200) {
    a.cls = ContentClass::RANDOM;
    return;
  }

  // Repetitive (long runs or repeated blocks).
  if (runs >= 0.30 || (H < 2.0 && a.top8_fraction >= 0.7)) {
    a.cls = ContentClass::REPETITIVE;
    return;
  }

  const bool texty = printable >= 0.85;
  const bool codey = printable >= 0.75 && a.newline_density > 8.0;

  if (texty || codey) {
    // Single-character structural proxies from the histogram.
    const auto c = [&](unsigned char ch) -> std::uint64_t {
      return a.hist[ch];
    };
    const double frac = static_cast<double>(n);
    const double braces = static_cast<double>(c('{') + c('}') + c('[') + c(']')) / frac;
    const double quotes = static_cast<double>(c('"')) / frac;
    const double commas = static_cast<double>(c(',')) / frac;
    const double angles = static_cast<double>(c('<') + c('>')) / frac;
    const double code_chars = static_cast<double>(c('{') + c('}') + c('(') + c(')') +
                                                 c('=') + c(';')) / frac;

    // JSON: brace + quote + colon heavy, few newlines.
    if (braces >= 0.03 && quotes >= 0.01 && a.newline_density < 15.0) {
      a.cls = ContentClass::JSON;
      return;
    }
    // XML: angle-bracket heavy with tags.
    if (angles >= 0.03) {
      a.cls = ContentClass::XML;
      return;
    }
    // LOG: text, very high newline density, lower entropy.
    if (a.newline_density > 40.0 && H < 5.5) {
      a.cls = ContentClass::LOG;
      return;
    }
    // CSV: comma + newline heavy, low entropy.
    if (commas >= 0.01 && a.newline_density > 10.0 && H < 5.0) {
      a.cls = ContentClass::CSV;
      return;
    }
    // SOURCE_CODE: code structural characters + newlines.
    if (codey && code_chars >= 0.02) {
      a.cls = ContentClass::SOURCE_CODE;
      return;
    }
    a.cls = ContentClass::TEXT;
    return;
  }

  // Executable-like: high entropy binary with code-ish byte distribution.
  if (zeros < 0.35) {
    a.cls = ContentClass::BINARY;
    return;
  }

  // Structured binary / database-like.
  if (zeros >= 0.10 && a.distinct < 160) {
    a.cls = ContentClass::DATABASE;
    return;
  }

  a.cls = ContentClass::MIXED;
}

bool effectively_incompressible(const Analysis& a, double threshold) noexcept {
  // Entropy > (8 - threshold) means the data is within `threshold` bits of
  // maximum entropy; modeling would buy almost nothing.
  return a.entropy >= 8.0 - threshold || a.cls == ContentClass::RANDOM;
}

double estimated_ratio(const Analysis& a) noexcept {
  if (a.sample_size == 0 || a.entropy <= 0.0) return 1.0;
  // Upper bound from entropy: 8 / H.
  const double entropy_ratio = 8.0 / a.entropy;
  // Repetition can push the effective ratio higher than the entropy bound
  // when runs dominate.
  double rep = 1.0;
  if (a.run_fraction > 0.2) rep = 1.0 + (a.run_fraction - 0.2) * 4.0;
  const double ratio = entropy_ratio * rep;
  // Clamp: never claim better than 1000x on a sample, never worse than 1x.
  if (ratio > 1000.0) return 1000.0;
  if (ratio < 1.0) return 1.0;
  return ratio;
}

std::string summarize(const Analysis& a) {
  std::string s;
  s += class_name(a.cls);
  if (!a.subtype.empty()) s += " (" + a.subtype + ")";
  s += " entropy=" + std::to_string(a.entropy).substr(0, 5);
  s += " est_ratio=" + std::to_string(estimated_ratio(a)).substr(0, 6);
  return s;
}

std::string to_json(const Analysis& a) {
  std::string s;
  s += "{\n";
  s += "  \"class\": \"" + std::string(class_name(a.cls)) + "\",\n";
  if (!a.subtype.empty()) s += "  \"subtype\": \"" + a.subtype + "\",\n";
  s += "  \"sample_size\": " + std::to_string(a.sample_size) + ",\n";
  s += "  \"entropy\": " + std::to_string(a.entropy) + ",\n";
  s += "  \"distinct_bytes\": " + std::to_string(a.distinct) + ",\n";
  s += "  \"printable_fraction\": " + std::to_string(a.printable_fraction) + ",\n";
  s += "  \"zero_fraction\": " + std::to_string(a.zero_fraction) + ",\n";
  s += "  \"run_fraction\": " + std::to_string(a.run_fraction) + ",\n";
  s += "  \"estimated_ratio\": " + std::to_string(estimated_ratio(a)) + ",\n";
  s += "  \"incompressible\": " +
       std::string(effectively_incompressible(a) ? "true" : "false") + "\n";
  s += "}";
  return s;
}

} // namespace zpaq_ng::analyzer