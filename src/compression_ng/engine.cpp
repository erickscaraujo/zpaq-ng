// engine.cpp - Next-generation compression engine implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression_ng/engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>

#include "analyzer/analyzer.hpp"
#include "archive/jidac.hpp"
#include "chunking/chunking.hpp"
#include "compression/compress_block.hpp"
#include "threading/thread_pool.hpp"

namespace zpaq_ng::compression_ng {

using clock = std::chrono::steady_clock;

namespace {

// Target block size (bytes) for each ng level.
std::size_t level_block_size(int level) {
  const std::size_t mb[] = {0, 4, 8, 16, 16, 32, 64, 64, 128, 256};
  const int l = std::clamp(level, 0, 9);
  return mb[l] * (1024u * 1024u);
}

} // namespace

std::string choose_method(int ng_level, analyzer::ContentClass cls) {
  const int level = std::clamp(ng_level, 0, 9);
  if (level == 0) return "0";
  if (ng_level <= 5) return std::string(1, char('0' + level));

  // ng6..ng9: same level-5 model family, progressively larger blocks (bigger
  // LZ77 hash tables and longer contexts). These are legacy methods too.
  const std::string block = std::to_string(level_block_size(level) >> 20);
  switch (cls) {
    case analyzer::ContentClass::TEXT:
    case analyzer::ContentClass::SOURCE_CODE:
    case analyzer::ContentClass::JSON:
    case analyzer::ContentClass::XML:
    case analyzer::ContentClass::CSV:
    case analyzer::ContentClass::LOG:
      return "5" + block;  // level 5, block 2^B MiB (text models via type arg)
    case analyzer::ContentClass::EXECUTABLE:
    case analyzer::ContentClass::BINARY:
      return "5" + block;  // E8E9 handled by the digit-method type analysis
    default:
      return "5" + block;
  }
}

std::string compress_block_adaptive(io::Buffer& in, io::Writer& out,
                                    const char* filename, const char* comment,
                                    const NgOptions& opt,
                                    unsigned* stored_out) {
  // Analyze a bounded sample of the block.
  const byte* p = in.data();
  const std::size_t n = in.size();
  analyzer::Analysis a = analyzer::analyze({p, std::min<std::size_t>(n, 1u << 20)});

  std::string method = choose_method(opt.level, a.cls);
  if (opt.store_incompressible && opt.level > 0 &&
      analyzer::effectively_incompressible(a)) {
    method = "0";
  }
  if (stored_out && method == "0") ++*stored_out;
  compression::compress_block(&in, &out, method.c_str(), filename, comment,
                              opt.verify);
  return method;
}

void compress_stream_ng(const std::vector<std::string>& files, io::Writer& out,
                        const NgOptions& opt, NgStats& stats) {
  const auto t0 = clock::now();
  const std::size_t mem_budget = opt.memory_limit == 0
                                     ? (std::size_t{512} * 1024 * 1024)
                                     : opt.memory_limit;
  const std::size_t block_size =
      opt.block_size == 0 ? level_block_size(opt.level) : opt.block_size;
  const std::size_t chunk_target =
      opt.chunk_size == 0 ? block_size : opt.chunk_size;

  threading::ThreadPool pool(opt.threads);
  threading::OrderedCollector<io::MemoryWriter> collector;
  std::size_t next_index = 0;
  std::size_t input_total = 0;
  std::atomic<unsigned> stored_total{0};
  std::mutex method_mtx;
  std::map<std::string, unsigned> method_use;

  const auto submit_blocks = [&](io::Buffer& data, std::string fname,
                                 std::string comment) {
    input_total += data.size();
    // Split the in-memory data into blocks.
    std::vector<std::size_t> bounds;
    if (opt.chunking && data.size() > block_size) {
      bounds = chunking::split({data.data(), data.size()}, block_size / 8,
                               chunk_target, block_size * 2);
    } else {
      bounds.push_back(0);
      for (std::size_t off = block_size; off < data.size(); off += block_size)
        bounds.push_back(off);
      bounds.push_back(data.size());
    }

    const std::size_t nblocks = bounds.size() - 1;
    for (std::size_t b = 0; b < nblocks; ++b) {
      const std::size_t start = bounds[b];
      const std::size_t len = bounds[b + 1] - start;
      const std::size_t index = next_index++;
      io::Buffer block_data;
      block_data.write(reinterpret_cast<const char*>(data.data() + start), len);
      std::string bname = fname;
      if (nblocks > 1) bname += ".part" + std::to_string(b + 1);
      pool.submit([&collector, &method_mtx, &method_use, &stored_total, index,
                   bname = std::move(bname), comment, opt,
                   data = std::move(block_data)]() mutable {
        io::MemoryWriter mw;
        unsigned stored = 0;
        const std::string method =
            compress_block_adaptive(data, mw, bname.c_str(), comment.c_str(),
                                    opt, &stored);
        stored_total.fetch_add(stored, std::memory_order_relaxed);
        {
          std::lock_guard<std::mutex> lock(method_mtx);
          ++method_use[method];
        }
        collector.push(index, std::move(mw));
      });
    }
  };

  for (const auto& f : files) {
    io::Buffer data;
    {
      // Read the file; skip if unreadable.
      archive::FileIn in(f.c_str());
      if (!in.is_open()) {
        std::fprintf(stderr, "create: %s not found\n", f.c_str());
        continue;
      }
      char buf[1u << 16];
      std::size_t r;
      while ((r = in.read(buf, sizeof buf)) > 0) data.write(buf, r);
    }
    if (data.size() > mem_budget) {
      std::fprintf(stderr,
                   "create: %s is %.1f MiB, above the in-memory budget "
                   "(%.1f MiB); using fixed-size blocks\n",
                   f.c_str(), data.size() / 1e6, mem_budget / 1e6);
    }
    // comment in the streaming convention: YYYYMMDDHHMMSS date.
    const std::string comment =
        archive::itoa64(archive::decimal_time(std::time(nullptr)));
    submit_blocks(data, f, comment);
  }

  pool.wait();

  // Drain in order.
  const std::size_t total = next_index;
  std::size_t output_total = 0;
  for (std::size_t i = 0; i < total; ++i) {
    io::MemoryWriter mw = collector.take(i);
    const auto& bytes = mw.bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    output_total += bytes.size();
  }

  stats.compress_seconds =
      std::chrono::duration<double>(clock::now() - t0).count();
  stats.input_bytes = input_total;
  stats.output_bytes = output_total;
  stats.blocks = static_cast<unsigned>(total);
  stats.stored_blocks = stored_total.load(std::memory_order_relaxed);
  std::string best;
  unsigned bestn = 0;
  for (const auto& [m, n] : method_use)
    if (n > bestn) { bestn = n; best = m; }
  stats.method = best;
}

} // namespace zpaq_ng::compression_ng