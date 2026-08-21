// recovery_test.cpp - Corruption detection and partial recovery tests.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Builds an archive with several stored (incompressible) blocks, verifies that
// an intact archive recovers fully, then corrupts bytes inside one block's
// payload and verifies that only that block's data is lost.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "compression_ng/engine.hpp"
#include "core/types.hpp"
#include "io/streams.hpp"
#include "recovery/recovery.hpp"

using zpaq_ng::byte;

namespace {

bool fail(const char* msg) {
  std::fprintf(stderr, "recovery_test: FAIL: %s\n", msg);
  return false;
}

// Deterministic pseudo-random bytes (LCG), never compressible.
std::vector<byte> random_bytes(std::size_t n, unsigned seed) {
  std::vector<byte> v(n);
  unsigned s = seed;
  for (std::size_t i = 0; i < n; ++i) {
    s = s * 1664525u + 1013904223u;
    v[i] = static_cast<byte>(s >> 24);
  }
  return v;
}

// Write a file, compress it with the NG engine into memory, and return the
// archive bytes. Uses store-only level 1 so every block is a stored segment
// (exercises the raw data path and SHA-1 verification).
std::vector<byte> make_archive(const std::vector<byte>& data,
                               const std::string& path,
                               std::size_t block_size) {
  {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) std::abort();
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
  }
  zpaq_ng::io::MemoryWriter out;
  zpaq_ng::compression_ng::NgOptions opt;
  opt.level = 1;
  opt.threads = 1;
  opt.block_size = block_size;
  zpaq_ng::compression_ng::NgStats stats;
  zpaq_ng::compression_ng::compress_stream_ng({path}, out, opt, stats);
  return out.bytes();
}

// Corrupt count bytes at archive offset off.
void corrupt(std::vector<byte>& archive, std::size_t off, std::size_t count) {
  for (std::size_t i = off; i < off + count && i < archive.size(); ++i)
    archive[i] = static_cast<byte>(archive[i] ^ 0xA5);
}

// Locate the "zPQ" block tags.
std::vector<std::size_t> tags(const std::vector<byte>& archive) {
  std::vector<std::size_t> t;
  for (std::size_t i = 0; i + 3 <= archive.size(); ++i)
    if (archive[i] == 'z' && archive[i + 1] == 'P' && archive[i + 2] == 'Q')
      t.push_back(i);
  return t;
}

} // namespace

int main() {
  const std::size_t block_size = 131072;
  const std::size_t n = 3 * block_size + 40000;  // 4 blocks
  const std::string path = "recovery_test_input.bin";
  const auto data = random_bytes(n, 42);
  auto archive = make_archive(data, path, block_size);
  std::remove(path.c_str());

  // 1. Intact archive: every block OK, output identical to the input.
  {
    zpaq_ng::io::MemoryWriter out;
    const auto rep = zpaq_ng::recovery::recover_stream(
        {archive.data(), archive.size()}, &out);
    if (rep.corrupted != 0)
      return fail("intact archive reported corrupted blocks");
    if (rep.ok == 0)
      return fail("intact archive reported no OK blocks");
    if (rep.recovered != 0)
      return fail("intact archive reported recovered blocks");
    const auto& bytes = out.bytes();
    if (bytes.size() != data.size() || std::memcmp(bytes.data(), data.data(),
                                                    data.size()) != 0)
      return fail("intact recovery output differs from input");
    std::printf("recovery_test: intact archive: %zu blocks OK, %zu bytes\n",
                rep.ok, bytes.size());
  }

  // 2. Corrupt inside the second block's payload (tag 1, +32 bytes).
  const auto t = tags(archive);
  if (t.size() != 4)
    return fail("expected 4 blocks in archive");
  auto bad = archive;
  corrupt(bad, t[1] + 32, 40);

  {
    zpaq_ng::io::MemoryWriter out;
    const auto rep = zpaq_ng::recovery::recover_stream(
        {bad.data(), bad.size()}, &out);
    if (rep.corrupted == 0)
      return fail("corrupted archive reported no corrupted blocks");
    const auto& bytes = out.bytes();

    // The second chunk (input [block_size, 2*block_size)) is lost; everything
    // else must be byte-exact.
    const std::size_t expected_size = n - block_size;
    if (bytes.size() != expected_size)
      return fail("recovered output size is wrong");
    bool mismatch = false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      const std::size_t src = i < block_size ? i : i + block_size;
      if (bytes[i] != data[src]) {
        mismatch = true;
        break;
      }
    }
    if (mismatch)
      return fail("recovered output differs from expected bytes");
    std::printf(
        "recovery_test: corrupted archive: %zu corrupted, %zu OK, %zu "
        "recovered, %zu bytes\n",
        rep.corrupted, rep.ok, rep.recovered, bytes.size());
  }

  std::printf("recovery_test: OK\n");
  return 0;
}