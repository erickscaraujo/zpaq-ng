// engine.hpp - Next-generation compression engine.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The NG engine wraps the legacy ZPAQ block compressor with adaptive strategy
// selection driven by the content analyzer. It never invents methods: every
// method it chooses is a valid legacy method, so NG archives remain readable
// by the original zpaq. Selection policy:
//   - Near-random data  -> store raw ("0"), skipping expensive modeling.
//   - Text/source/...   -> level + text-oriented method.
//   - Executable        -> level + E8E9-friendly method.
//   - Level 0          -> store everything.
// The block/level mapping is fixed and verified; ngN does not claim a higher
// ratio than ngN-1 without benchmark evidence.

#ifndef ZPAQ_NG_COMPRESSION_NG_ENGINE_HPP
#define ZPAQ_NG_COMPRESSION_NG_ENGINE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "analyzer/analyzer.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::compression_ng {

// NG compression options.
struct NgOptions {
  int level = 1;              // 0..9. 0 = store only. 1..5 = legacy levels.
  unsigned threads = 1;       // 0 = hardware concurrency.
  std::size_t block_size = 0; // target block bytes (0 = derived from level).
  std::size_t chunk_size = 0; // content-defined chunk target (0 = block_size).
  bool chunking = false;      // split blocks on content-defined boundaries.
  bool verify = true;         // write SHA-1 checksums (true = compatible).
  bool store_incompressible = true;  // store near-random blocks instead of modeling.
  std::size_t memory_limit = 0;      // per-file in-memory budget (0 = 512 MiB).
};

// Per-run statistics (also the JSON shape for `benchmark`/`create --json`).
struct NgStats {
  std::size_t input_bytes = 0;
  std::size_t output_bytes = 0;
  double compress_seconds = 0.0;
  double decompress_seconds = 0.0;
  unsigned blocks = 0;
  unsigned stored_blocks = 0;  // blocks stored raw by the fast path
  unsigned deduped_chunks = 0; // CDC chunks referencing a stored chunk
  std::string method;          // the dominant method used
  double compression_ratio() const {
    return output_bytes == 0 ? 1.0
                             : static_cast<double>(input_bytes) /
                                   static_cast<double>(output_bytes);
  }
  double compression_mbps() const {
    return compress_seconds > 0.0
               ? input_bytes / 1e6 / compress_seconds
               : 0.0;
  }
  double decompression_mbps() const {
    return decompress_seconds > 0.0
               ? input_bytes / 1e6 / decompress_seconds
               : 0.0;
  }
};

// Map an ng level to a legacy method string for a given content class.
// Returns "0" for store-only. The result is always a method the original zpaq
// understands.
std::string choose_method(int ng_level, analyzer::ContentClass cls);

// Compress a single in-memory block with adaptive strategy selection. Writes a
// complete ZPAQ block (tag + header + one segment) to out. The input buffer is
// drained (compress_block resets it). Returns the method used.
std::string compress_block_adaptive(io::Buffer& in, io::Writer& out,
                                    const char* filename, const char* comment,
                                    const NgOptions& opt,
                                    unsigned* stored_out = nullptr);

// Compress a list of files into a streaming ZPAQ archive (one block per chunk,
// chunking on content-defined boundaries when opt.chunking is set). Blocks are
// compressed in parallel (opt.threads) and written in deterministic order.
// Fills stats with measured numbers.
void compress_stream_ng(const std::vector<std::string>& files, io::Writer& out,
                        const NgOptions& opt, NgStats& stats);

} // namespace zpaq_ng::compression_ng

#endif // ZPAQ_NG_COMPRESSION_NG_ENGINE_HPP