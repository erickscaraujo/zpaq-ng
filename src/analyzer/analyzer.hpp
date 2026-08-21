// analyzer.hpp - Content analyzer and classification.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Deterministic, statistical heuristics only. No machine learning. The cost of
// analysis is bounded (a sample of up to max_sample_bytes is examined) and is
// small compared to the cost of compression itself.

#ifndef ZPAQ_NG_ANALYZER_ANALYZER_HPP
#define ZPAQ_NG_ANALYZER_ANALYZER_HPP

#include <array>
#include <cstdint>
#include <string>

#include "core/types.hpp"

namespace zpaq_ng::analyzer {

// Content classes produced by the analyzer.
enum class ContentClass {
  TEXT,        // plain text
  SOURCE_CODE, // programming language source
  JSON,
  XML,
  CSV,
  LOG,
  DATABASE,    // fixed-width / columnar records
  BINARY,
  EXECUTABLE,
  SCIENTIFIC,  // high entropy but structured (floats, time series)
  REPETITIVE,  // long runs or repeated blocks
  RANDOM,      // near-maximum entropy, incompressible
  MIXED,
  UNKNOWN,
};

const char* class_name(ContentClass c) noexcept;

struct Analysis {
  std::size_t sample_size = 0;      // bytes actually analyzed
  double entropy = 0.0;             // Shannon entropy, bits per byte (0..8)
  std::array<std::uint32_t, 256> hist{};  // byte histogram of the sample
  std::uint32_t distinct = 0;       // distinct byte values seen
  double printable_fraction = 0.0;  // printable ASCII + space/tab/newline
  double zero_fraction = 0.0;       // 0x00 density
  double run_fraction = 0.0;        // fraction of bytes equal to predecessor
  double newline_density = 0.0;     // '\n' per KiB
  double top8_fraction = 0.0;       // mass of the 8 most frequent bytes
  ContentClass cls = ContentClass::UNKNOWN;
  std::string subtype;              // e.g. "PE", "ELF", "DOS", "gzip", ""
};

// Analyze a bounded sample. At most max_sample bytes are examined (the caller
// may sample larger inputs first, e.g. evenly spaced).
Analysis analyze(ConstBytes sample, std::size_t max_sample = 1u << 20);

// Fill in cls/subtype from the statistics in a.
void classify(Analysis& a);

// True when the data is estimated to be essentially incompressible
// (near-random), so storing it raw is preferable to modeling it.
bool effectively_incompressible(const Analysis& a, double threshold = 0.10) noexcept;

// Rough estimate of the best achievable compression ratio (input/output),
// 1.0 means incompressible. Derived from entropy plus repetition.
double estimated_ratio(const Analysis& a) noexcept;

// A short human-readable summary line (used by --verbose).
std::string summarize(const Analysis& a);

// JSON representation of the analysis (used by --json).
std::string to_json(const Analysis& a);

} // namespace zpaq_ng::analyzer

#endif // ZPAQ_NG_ANALYZER_ANALYZER_HPP