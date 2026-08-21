// ng_cli.cpp - NG command handlers implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "cli/ng_cli.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "archive/jidac.hpp"
#include "benchmark/benchmark.hpp"
#include "compression_ng/engine.hpp"
#include "core/error.hpp"
#include "hardware/hardware.hpp"
#include "matching/matching.hpp"
#include "recovery/recovery.hpp"

namespace zpaq_ng::cli {

namespace fs = std::filesystem;

namespace {

std::size_t parse_size(const std::string& s, const char* what) {
  if (s == "auto" || s.empty()) return 0;
  std::size_t v = 0;
  for (char c : s) {
    if (c >= '0' && c <= '9')
      v = v * 10 + static_cast<std::size_t>(c - '0');
    else if (c == 'M' || c == 'm')
      v *= 1024u * 1024u;
    else if (c == 'G' || c == 'g')
      v *= 1024u * 1024u * 1024u;
    else if (c == 'K' || c == 'k')
      v *= 1024u;
    else if (c == 'B' || c == 'b' || c == ' ')
      ;
    else
      throw invalid_argument_error(std::string(what) + ": bad size \"" + s + "\"");
  }
  return v;
}

int parse_level(const std::string& s) {
  if (s == "auto") return 1;
  if (s.size() >= 2 && s[0] == 'n' && s[1] == 'g')
    return std::atoi(s.c_str() + 2);
  return std::atoi(s.c_str());
}

} // namespace

void parse_ng_options(int argc, const char** argv, NgCliOptions& opt,
                      std::vector<std::string>& positional) {
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--level") {
      if (++i >= argc) throw invalid_argument_error("--level needs a value");
      opt.level = parse_level(argv[i]);
      if (opt.level < 0 || opt.level > 9)
        throw invalid_argument_error("--level must be ng0..ng9 or 0..9");
    } else if (a == "--threads") {
      if (++i >= argc) throw invalid_argument_error("--threads needs a value");
      const std::string v = argv[i];
      opt.threads = (v == "auto") ? 0 : static_cast<unsigned>(std::atoi(v.c_str()));
    } else if (a == "--device") {
      if (++i >= argc) throw invalid_argument_error("--device needs a value");
      opt.device = argv[i];
    } else if (a == "--memory") {
      if (++i >= argc) throw invalid_argument_error("--memory needs a value");
      opt.memory = parse_size(argv[i], "--memory");
    } else if (a == "--dictionary") {
      if (++i >= argc) throw invalid_argument_error("--dictionary needs a value");
      opt.dictionary = parse_size(argv[i], "--dictionary");
    } else if (a == "--chunk-size") {
      if (++i >= argc) throw invalid_argument_error("--chunk-size needs a value");
      opt.chunk_size = parse_size(argv[i], "--chunk-size");
    } else if (a == "--dedup") {
      opt.dedup = true;
    } else if (a == "--verify") {
      opt.verify = true;
    } else if (a == "--no-verify") {
      opt.verify = false;
    } else if (a == "--progress") {
      opt.progress = true;
    } else if (a == "--json") {
      opt.json = true;
    } else if (a == "--verbose") {
      opt.verbose = true;
    } else if (a == "--deterministic") {
      opt.deterministic = true;
    } else if (a == "--compare") {
      opt.compare = true;
    } else if (!a.empty() && a[0] == '-') {
      throw invalid_argument_error("unknown option: " + a);
    } else {
      positional.push_back(a);
    }
  }
}

int command_create(const NgCliOptions& opt, const std::vector<std::string>& args) {
  if (args.size() < 2)
    throw invalid_argument_error("create needs an archive and at least one file");
  const std::string archive = args[0];
  std::vector<std::string> files(args.begin() + 1, args.end());

  // Expand directories.
  std::vector<std::string> expanded;
  for (const auto& f : files) {
    if (fs::is_directory(f)) {
      for (const auto& e : fs::recursive_directory_iterator(f)) {
        if (e.is_regular_file()) expanded.push_back(e.path().string());
      }
    } else {
      expanded.push_back(f);
    }
  }
  if (expanded.empty()) {
    std::fprintf(stderr, "create: no input files\n");
    return 1;
  }

  // Device selection: v1.0 is CPU-only. Anything other than cpu/auto is
  // rejected honestly rather than emulated.
  if (opt.device != "auto" && opt.device != "cpu") {
    std::fprintf(stderr,
                 "create: device \"%s\" is not available in this build "
                 "(CPU-only); refusing to fake a backend\n",
                 opt.device.c_str());
    return 1;
  }

  compression_ng::NgOptions eng;
  eng.level = opt.level;
  eng.threads = opt.threads;
  eng.block_size = opt.dictionary;
  eng.chunk_size = opt.chunk_size;
  eng.chunking = opt.dedup;  // CDC chunking implements content-defined dedup blocks
  eng.verify = opt.verify;
  eng.memory_limit = opt.memory;

  archive::FileOut out(archive.c_str());
  if (!out.is_open()) {
    std::fprintf(stderr, "create: cannot open %s\n", archive.c_str());
    return 1;
  }

  compression_ng::NgStats stats;
  compression_ng::compress_stream_ng(expanded, out, eng, stats);
  out.flush();
  out.close();

  if (opt.json) {
    std::printf(
        "{\"command\":\"create\",\"archive\":\"%s\",\"input_size\":%zu,"
        "\"output_size\":%zu,\"compression_ratio\":%.4f,"
        "\"compression_time\":%.4f,\"compression_throughput_mbps\":%.2f,"
        "\"blocks\":%u,\"stored_blocks\":%u,\"method\":\"%s\",\"threads\":%u,"
        "\"compression_level\":%d,\"device\":\"cpu\"}\n",
        archive.c_str(), stats.input_bytes, stats.output_bytes,
        stats.compression_ratio(), stats.compress_seconds,
        stats.compression_mbps(), stats.blocks, stats.stored_blocks,
        stats.method.c_str(), opt.threads, opt.level);
  } else {
    std::printf("%zu -> %zu bytes, ratio %.4f, %.2f MB/s (%u blocks",
                stats.input_bytes, stats.output_bytes, stats.compression_ratio(),
                stats.compression_mbps(), stats.blocks);
    if (stats.stored_blocks > 0)
      std::printf(", %u stored incompressible", stats.stored_blocks);
    std::printf(")\n");
  }
  return 0;
}

int command_benchmark(const NgCliOptions& opt, const std::vector<std::string>& args) {
  std::string dir;
  if (!args.empty()) dir = args[0];
  compression_ng::NgOptions eng;
  eng.level = opt.level;
  eng.threads = opt.threads;
  eng.verify = true;
  const auto results = benchmark::run(eng, dir, opt.compare);
  if (opt.json)
    std::printf("%s\n", benchmark::to_json(results).c_str());
  else
    std::printf("%s", benchmark::report(results).c_str());
  return 0;
}

int command_devices(const NgCliOptions& opt) {
  const hardware::HardwareInfo h = hardware::detect();
  if (opt.json)
    std::printf("%s\n", hardware::to_json(h).c_str());
  else
    std::printf("%s", hardware::report(h).c_str());
  return 0;
}

int command_info(const NgCliOptions& opt, const std::vector<std::string>& args) {
  if (args.empty()) throw invalid_argument_error("info needs an archive");
  archive::Jidac j;
  j.archive = args[0];
  int errors = 0;
  j.read_archive(args[0].c_str(), &errors);
  if (opt.json) {
    std::printf(
        "{\"archive\":\"%s\",\"versions\":%zu,\"files\":%zu,"
        "\"fragments\":%zu,\"blocks\":%zu,\"index_errors\":%d}\n",
        args[0].c_str(), j.version_count(), j.dt_count(), j.fragment_count(),
        j.block_count(), errors);
  } else {
    std::printf("%s: %zu versions, %zu files, %zu fragments, %zu blocks\n",
                args[0].c_str(), j.version_count(), j.dt_count(),
                j.fragment_count(), j.block_count());
  }
  return 0;
}

int command_profile(const NgCliOptions& opt) {
  compression_ng::NgOptions eng;
  eng.threads = opt.threads;
  std::printf("ZPAQ-NG profile (synthetic corpus, measured)\n");
  for (int level = 1; level <= 5; ++level) {
    eng.level = level;
    const auto results = benchmark::run(eng, "", false, true);
    double ratio = 0, cm = 0, dm = 0;
    for (const auto& r : results) {
      ratio += r.ratio;
      cm += r.compress_mbps;
      dm += r.decompress_mbps;
    }
    const std::size_t n = results.empty() ? 1 : results.size();
    std::printf("  ng%d: ratio %.3f  comp %.1f MB/s  decomp %.1f MB/s\n",
                level, ratio / n, dm / n, cm / n);
  }

  // Match finder evaluation on a realistic repetitive sample (bounded so the
  // profile stays fast even on pathological inputs).
  {
    std::string sample;
    sample.reserve(1u << 20);
    const char* sentences[] = {
        "The quick brown fox jumps over the lazy dog. ",
        "Sphinx of black quartz, judge my vow. ",
        "Pack my box with five dozen liquor jugs. ",
    };
    for (std::size_t i = 0; i + 32 < (1u << 20); i += 32)
      sample += sentences[(i / 32) % 3];
    sample.resize(1u << 20);
    const byte* p = reinterpret_cast<const byte*>(sample.data());
    const auto hc = matching::benchmark_hash_chain({p, sample.size()});
    const auto rh = matching::benchmark_rolling({p, sample.size()});
    std::printf("\nMatch finders (1 MiB repetitive sample)\n");
    std::printf("  %-12s matches=%7zu  match_bytes=%9zu  %6.1f MB/s  mem=%zu\n",
                hc.name.c_str(), hc.matches, hc.match_bytes,
                hc.throughput_mbps(), hc.memory);
    std::printf("  %-12s matches=%7zu  match_bytes=%9zu  %6.1f MB/s  mem=%zu\n",
                rh.name.c_str(), rh.matches, rh.match_bytes,
                rh.throughput_mbps(), rh.memory);
  }
  return 0;
}

int command_recover(const NgCliOptions& opt, const std::vector<std::string>& args) {
  if (args.empty()) throw invalid_argument_error("recover needs an archive");
  const std::string arc = args[0];
  std::string outname = args.size() > 1 ? args[1] : "recovered.bin";
  std::FILE* f = std::fopen(arc.c_str(), "rb");
  if (!f) {
    std::fprintf(stderr, "recover: cannot open %s\n", arc.c_str());
    return 1;
  }
  std::vector<byte> data;
  char buf[1u << 16];
  std::size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0)
    data.insert(data.end(), buf, buf + r);
  std::fclose(f);

  std::FILE* out = std::fopen(outname.c_str(), "wb");
  if (!out) {
    std::fprintf(stderr, "recover: cannot open %s\n", outname.c_str());
    return 1;
  }
  io::MemoryWriter sink;
  const recovery::RecoveryReport rep =
      recovery::recover_stream({data.data(), data.size()}, &sink);
  const auto& bytes = sink.bytes();
  std::fwrite(bytes.data(), 1, bytes.size(), out);
  std::fclose(out);

  if (opt.json) {
    std::printf("{\"archive\":\"%s\",\"blocks_ok\":%zu,\"corrupted\":%zu,"
                "\"recovered\":%zu,\"output_bytes\":%zu}\n",
                arc.c_str(), rep.ok, rep.corrupted, rep.recovered,
                bytes.size());
  } else {
    std::printf("%s", recovery::report(rep).c_str());
  }
  return 0;
}

} // namespace zpaq_ng::cli