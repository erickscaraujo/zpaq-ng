// recovery.hpp - Corruption detection and partial recovery.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Extracts every valid block from a streaming ZPAQ archive, skipping corrupted
// blocks and reporting what was recovered. Data that cannot be recovered is
// never claimed to be recovered.

#ifndef ZPAQ_NG_RECOVERY_RECOVERY_HPP
#define ZPAQ_NG_RECOVERY_RECOVERY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::recovery {

struct BlockStatus {
  std::size_t index = 0;
  std::string status;  // "ok", "corrupted", "recovered"
  std::size_t output_bytes = 0;
};

struct RecoveryReport {
  std::vector<BlockStatus> blocks;
  std::size_t ok = 0;
  std::size_t corrupted = 0;
  std::size_t recovered = 0;  // blocks that were valid after a resync
  std::size_t output_bytes = 0;
};

// Decode a streaming ZPAQ archive held in memory. Valid blocks are written to
// out; corrupted blocks are skipped and reported. SHA-1 checksums present in
// segment trailers are verified when available.
RecoveryReport recover_stream(ConstBytes archive, io::Writer* out);

// Human readable report.
std::string report(const RecoveryReport& r);

} // namespace zpaq_ng::recovery

#endif // ZPAQ_NG_RECOVERY_RECOVERY_HPP