// benchmark.cpp - Benchmark engine implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "benchmark/benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <string>

#include "analyzer/analyzer.hpp"
#include "decompression/block_decoder.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::benchmark {

namespace fs = std::filesystem;
using clock = std::chrono::steady_clock;

namespace {

// A writer that discards bytes (for measuring decompression throughput).
class NullWriter final : public io::Writer {
public:
  void put(int) override { ++count_; }
  void write(const char* buf, std::size_t n) override {
    (void)buf;
    count_ += n;
  }
  std::size_t count_ = 0;
};

// A byte source over a contiguous memory buffer (seekable not required).
class MemReader final : public io::Reader {
public:
  explicit MemReader(const std::vector<byte>& v) : v_(v), pos_(0) {}
  int get() override { return pos_ < v_.size() ? v_[pos_++] : -1; }
  std::size_t read(char* buf, std::size_t n) override {
    const std::size_t take = std::min(n, v_.size() - pos_);
    std::memcpy(buf, v_.data() + pos_, take);
    pos_ += take;
    return take;
  }
  const std::vector<byte>& v_;
  std::size_t pos_;
};

// Deterministic PRNG for the synthetic corpus.
std::mt19937_64 rng(0x5EED1234ULL);

std::string word() {
  static const char* w[] = {"alpha", "bravo", "charlie", "delta", "echo",
                            "foxtrot", "golf", "hotel", "india", "juliet"};
  return w[rng() % 10];
}

std::string lorem(std::size_t n) {
  std::string s;
  s.reserve(n);
  while (s.size() < n) {
    for (int i = 0; i < 9; ++i) {
      s += word();
      s += " ";
    }
    s += "The quick brown fox jumps over the lazy dog. ";
  }
  s.resize(n);
  return s;
}

std::string source_code(std::size_t n) {
  std::string s;
  s.reserve(n);
  int indent = 0;
  while (s.size() < n) {
    const int r = int(rng() % 100);
    if (r < 40) {
      s.append(indent, ' ');
      s += "int var_" + std::to_string(rng() % 100) + " = " +
           std::to_string(rng() % 1000) + ";\n";
    } else if (r < 70) {
      s.append(indent, ' ');
      s += "if (x < " + std::to_string(rng() % 100) + ") {\n";
      indent += 2;
    } else if (r < 85) {
      s.append(indent, ' ');
      s += "}\n";
      indent = std::max(0, indent - 2);
    } else {
      s.append(indent, ' ');
      s += "for (i = 0; i < " + std::to_string(rng() % 100) +
           "; ++i) foo();\n";
    }
  }
  s.resize(n);
  return s;
}

std::string json_data(std::size_t n) {
  std::string s;
  s.reserve(n);
  while (s.size() < n) {
    s += "{\"user\":\"user" + std::to_string(rng() % 500) + "\",\"id\":" +
         std::to_string(rng() % 100000) +
         ",\"active\":true,\"tags\":[\"a\",\"b\",\"c\"],\"score\":" +
         std::to_string(rng() % 10000) + "},\n";
  }
  s.resize(n);
  return s;
}

std::string csv_data(std::size_t n) {
  std::string s;
  s.reserve(n);
  while (s.size() < n) {
    for (int c = 0; c < 8; ++c) {
      if (c) s += ',';
      s += std::to_string(rng() % 10000);
    }
    s += '\n';
  }
  s.resize(n);
  return s;
}

std::string log_data(std::size_t n) {
  std::string s;
  s.reserve(n);
  while (s.size() < n) {
    s += "2026-08-20T12:34:56 level=" + std::to_string(rng() % 6) +
         " module=app message=processing item " + std::to_string(rng() % 1000) +
         " ok\n";
  }
  s.resize(n);
  return s;
}

std::string db_data(std::size_t n) {
  std::string s(n, 0);
  for (std::size_t i = 0; i + 64 <= n; i += 64) {
    s[i] = 'R';
    s[i + 1] = char(rng() % 250 + 1);
    for (int k = 2; k < 32; ++k) s[i + k] = char(rng() % 250);
    for (int k = 32; k < 48; ++k) s[i + k] = char(rng() % 10);
  }
  return s;
}

std::string binary_data(std::size_t n) {
  std::string s(n, 0);
  for (std::size_t i = 0; i < n; ++i) s[i] = char(rng() % 250);
  return s;
}

std::string executable_data(std::size_t n) {
  std::string s = binary_data(n);
  const char magic[] = "MZ";
  std::memcpy(&s[0], magic, 2);
  for (std::size_t i = 8; i + 4 <= n && i < 1024; i += 64) {
    s[i] = 'P'; s[i + 1] = 'E';  // PE header hint
  }
  return s;
}

std::string scientific_data(std::size_t n) {
  std::string s(n, 0);
  for (std::size_t i = 0; i + 8 <= n; i += 8) {
    double v = double(rng() % 1000000) / 1000.0;
    std::memcpy(&s[i], &v, 8);
  }
  return s;
}

std::string repetitive_data(std::size_t n) {
  static const char block[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  std::string s;
  s.reserve(n);
  while (s.size() < n) s += block;
  s.resize(n);
  return s;
}

std::string random_data(std::size_t n) {
  std::string s(n, 0);
  for (std::size_t i = 0; i < n; ++i) s[i] = char(rng() % 256);
  return s;
}

std::string mixed_data(std::size_t n) {
  return lorem(n / 5) + source_code(n / 5) + json_data(n / 5) +
         csv_data(n / 5) + binary_data(n / 5);
}

// Create the synthetic corpus in-memory. Returns name -> data.
std::vector<std::pair<std::string, std::string>> corpus(std::size_t n) {
  std::vector<std::pair<std::string, std::string>> out;
  out.emplace_back("TEXT", lorem(n));
  out.emplace_back("SOURCE_CODE", source_code(n));
  out.emplace_back("JSON", json_data(n));
  out.emplace_back("CSV", csv_data(n));
  out.emplace_back("LOG", log_data(n));
  out.emplace_back("DATABASE", db_data(n));
  out.emplace_back("BINARY", binary_data(n));
  out.emplace_back("EXECUTABLE", executable_data(n));
  out.emplace_back("SCIENTIFIC", scientific_data(n));
  out.emplace_back("REPETITIVE", repetitive_data(n));
  out.emplace_back("RANDOM", random_data(n));
  out.emplace_back("MIXED", mixed_data(n));
  return out;
}

// Measure decompression of a streaming archive in memory. Returns seconds and
// the decoded byte count.
std::pair<double, std::size_t> measure_decompress(const std::vector<byte>& arc) {
  MemReader in(arc);
  NullWriter out;
  decompression::BlockDecoder dec;
  dec.set_input(&in);
  dec.set_output(&out);
  const auto t0 = clock::now();
  while (dec.find_block()) {
    while (dec.find_filename()) {
      dec.read_comment();
      while (dec.decompress(-1)) {
      }
      dec.read_segment_end();
    }
  }
  const double s = std::chrono::duration<double>(clock::now() - t0).count();
  return {s, out.count_};
}

BenchResult bench_one(const std::string& name, std::string data,
                      const compression_ng::NgOptions& opt) {
  BenchResult r;
  r.name = name;
  const std::vector<byte> input(data.begin(), data.end());
  r.input = input.size();
  const analyzer::Analysis a =
      analyzer::analyze({input.data(), std::min<std::size_t>(input.size(), 1u << 20)});
  r.cls = analyzer::class_name(a.cls);

  io::MemoryWriter arc;
  compression_ng::NgStats st;
  const auto t0 = clock::now();
  // Write the data to a temp file so the engine can read it back.
  const fs::path tmp = fs::temp_directory_path() / ("zpaq_ng_bench_" + name);
  {
    std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
    if (f) {
      std::fwrite(data.data(), 1, data.size(), f);
      std::fclose(f);
    }
  }
  const std::vector<std::string> files{tmp.string()};
  compression_ng::compress_stream_ng(files, arc, opt, st);
  const double cs = std::chrono::duration<double>(clock::now() - t0).count();
  std::error_code ec;
  fs::remove(tmp, ec);

  const std::vector<byte>& bytes = arc.bytes();
  r.output = bytes.size();
  r.compress_seconds = cs;
  r.decompress_seconds = 0.0;
  const auto ds = measure_decompress(bytes);
  r.decompress_seconds = ds.first;
  r.ratio = r.output == 0 ? 1.0 : double(r.input) / double(r.output);
  r.compress_mbps = cs > 0 ? r.input / 1e6 / cs : 0.0;
  r.decompress_mbps = ds.first > 0 ? r.input / 1e6 / ds.first : 0.0;
  r.method = st.method;
  r.blocks = st.blocks;
  r.stored = st.stored_blocks;
  return r;
}

} // namespace

std::vector<BenchResult> run(const compression_ng::NgOptions& opt,
                             const std::string& dir, bool compare, bool quick) {
  std::vector<BenchResult> results;
  if (dir.empty()) {
    const std::size_t n = quick ? (1u << 20) : (4u << 20);  // per-dataset bytes
    for (auto& [name, data] : corpus(n)) {
      results.push_back(bench_one(name, std::move(data), opt));
      if (compare) {
        compression_ng::NgOptions o = opt;
        o.level = std::min(o.level + 1, 5);
        results.push_back(bench_one(name + " (level" + std::to_string(o.level) + ")",
                                    std::move(data), o));
      }
    }
  } else {
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied);
    for (const auto& e : it) {
      if (!e.is_regular_file()) continue;
      std::FILE* f = std::fopen(e.path().string().c_str(), "rb");
      if (!f) continue;
      std::string data;
      char buf[1u << 16];
      std::size_t r;
      while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
      std::fclose(f);
      results.push_back(bench_one(e.path().filename().string(), std::move(data), opt));
    }
  }
  return results;
}

std::string report(const std::vector<BenchResult>& rs) {
  std::string s;
  s += "ZPAQ-NG Benchmark\n";
  s += "-----------------\n";
  s += "name                class       in(MB)  out(MB)  ratio  comp(MB/s)  decomp(MB/s)  blocks  stored  method\n";
  for (const auto& r : rs) {
    char line[512];
    std::snprintf(line, sizeof line,
                  "%-18s %-12s %7.1f  %7.1f  %5.2f  %9.1f  %12.1f  %6u  %6u  %s\n",
                  r.name.c_str(), r.cls.c_str(), r.input / 1e6, r.output / 1e6,
                  r.ratio, r.compress_mbps, r.decompress_mbps, r.blocks,
                  r.stored, r.method.c_str());
    s += line;
  }
  return s;
}

std::string to_json(const std::vector<BenchResult>& rs) {
  std::string s = "[\n";
  for (std::size_t i = 0; i < rs.size(); ++i) {
    const auto& r = rs[i];
    s += "  {\n";
    s += "    \"name\": \"" + r.name + "\",\n";
    s += "    \"class\": \"" + r.cls + "\",\n";
    s += "    \"input_size\": " + std::to_string(r.input) + ",\n";
    s += "    \"output_size\": " + std::to_string(r.output) + ",\n";
    s += "    \"compression_ratio\": " + std::to_string(r.ratio) + ",\n";
    s += "    \"compression_time\": " + std::to_string(r.compress_seconds) + ",\n";
    s += "    \"decompression_time\": " + std::to_string(r.decompress_seconds) + ",\n";
    s += "    \"compression_throughput_mbps\": " + std::to_string(r.compress_mbps) + ",\n";
    s += "    \"decompression_throughput_mbps\": " + std::to_string(r.decompress_mbps) + ",\n";
    s += "    \"blocks\": " + std::to_string(r.blocks) + ",\n";
    s += "    \"stored_blocks\": " + std::to_string(r.stored) + ",\n";
    s += "    \"algorithm\": \"" + r.method + "\"\n";
    s += "  }" + (i + 1 < rs.size() ? std::string(",") : std::string()) + "\n";
  }
  s += "]";
  return s;
}

} // namespace zpaq_ng::benchmark