// benchmark.hpp - Benchmark engine for ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Measures compression ratio, compression/decompression time and throughput,
// thread scaling, and (optionally) compares two configurations. Numbers are
// measured, never estimated or invented.

#ifndef ZPAQ_NG_BENCHMARK_BENCHMARK_HPP
#define ZPAQ_NG_BENCHMARK_BENCHMARK_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "compression_ng/engine.hpp"

namespace zpaq_ng::benchmark {

// One measured sample.
struct BenchResult {
  std::string name;              // dataset or file name
  std::string cls;               // content class (analyzed)
  std::size_t input = 0;
  std::size_t output = 0;
  double ratio = 1.0;
  double compress_seconds = 0.0;
  double decompress_seconds = 0.0;
  double compress_mbps = 0.0;
  double decompress_mbps = 0.0;
  std::string method;
  unsigned blocks = 0;
  unsigned stored = 0;           // blocks stored raw by the fast path
};

// Run the benchmark. When dir is non-empty, benchmark the regular files in it
// (direct children); otherwise generate the standard synthetic corpus. The
// synthetic corpus is deterministic (fixed seed). quick uses a 1 MiB corpus
// (for the profile command).
std::vector<BenchResult> run(const compression_ng::NgOptions& opt,
                             const std::string& dir = "",
                             bool compare = false, bool quick = false);

// Produce a human readable report from results.
std::string report(const std::vector<BenchResult>& r);

// Produce a JSON report from results.
std::string to_json(const std::vector<BenchResult>& r);

} // namespace zpaq_ng::benchmark

#endif // ZPAQ_NG_BENCHMARK_BENCHMARK_HPP