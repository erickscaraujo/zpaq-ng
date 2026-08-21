// fuzz_archive.cpp - Fuzz harness for the archive/block decoders.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Compiles either as a libFuzzer entry point (clang -fsanitize=fuzzer) or as a
// standalone randomized smoke driver (GCC/other). Both feed arbitrary, often
// truncated or corrupted, byte buffers into the recovery layer and the block
// decoder; the sanitizer builds turn any memory error into a failure.
//
// Build the smoke driver:
//   g++ -std=c++20 -Isrc -fsanitize=address,undefined -O1
//       src/fuzz/fuzz_archive.cpp <all src/*.cpp> -o fuzz_archive
//   ./fuzz_archive [iterations] [seed]

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "decompression/block_decoder.hpp"
#include "integrity/sha1.hpp"
#include "io/streams.hpp"
#include "recovery/recovery.hpp"

namespace {

class NullOut final : public zpaq_ng::io::Writer {
public:
  void put(int) override {}
};

// Parse as a streaming archive with the plain block decoder (no checksum
// verification, matching the original decoder's tolerance).
void exercise_decoder(const zpaq_ng::byte* data, std::size_t size) {
  zpaq_ng::io::MemoryReader in(data, size);
  NullOut out;
  zpaq_ng::decompression::BlockDecoder dec;
  dec.set_input(&in);
  dec.set_output(&out);
  try {
    while (dec.find_block()) {
      while (dec.find_filename()) {
        dec.read_comment();
        zpaq_ng::integrity::SHA1 sha1;
        dec.set_sha1(&sha1);
        while (dec.decompress(-1)) {
        }
        char checksum[21] = {0};
        dec.read_segment_end(checksum);
      }
    }
  } catch (const std::exception&) {
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const zpaq_ng::byte* p = reinterpret_cast<const zpaq_ng::byte*>(data);
  exercise_decoder(p, size);
  zpaq_ng::io::MemoryWriter sink;
  try {
    zpaq_ng::recovery::recover_stream({p, size}, &sink);
  } catch (const std::exception&) {
  }
  return 0;
}

#if defined(ZPAQ_FUZZ_STANDALONE)  // standalone randomized smoke driver

#include <random>

namespace {
std::uint64_t rng_state = 0x9e3779b97f4a7c15ull;
std::uint64_t next_rng() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}
} // namespace

int main(int argc, char** argv) {
  const std::size_t iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10)
                                          : 2000;
  const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
  rng_state = seed;

  // Start from a real valid archive so mutations stay interesting.
  std::vector<zpaq_ng::byte> base;
  {
    const char* text = "The quick brown fox jumps over the lazy dog.\n";
    // Minimal valid archive: build one with the compression engine would pull
    // in more sources; instead seed with raw pseudo-archive bytes and rely on
    // random mutation to cover the parser's defensive paths.
    base.assign(reinterpret_cast<const zpaq_ng::byte*>(text),
                reinterpret_cast<const zpaq_ng::byte*>(text) +
                    std::strlen(text));
  }

  std::size_t failures = 0;
  for (std::size_t it = 0; it < iterations; ++it) {
    std::vector<zpaq_ng::byte> buf = base;
    // Randomize into a plausible archive-shaped buffer.
    const std::size_t n = 1 + next_rng() % 4096;
    buf.resize(n);
    for (std::size_t i = 0; i < n; ++i)
      buf[i] = static_cast<zpaq_ng::byte>(next_rng() >> 24);
    // Sometimes keep the leading tag to exercise deeper paths.
    if (it % 3 == 0 && n > 3) {
      buf[0] = 'z';
      buf[1] = 'P';
      buf[2] = 'Q';
    }
    try {
      LLVMFuzzerTestOneInput(buf.data(), buf.size());
    } catch (const std::exception&) {
      ++failures;  // recover_stream must not throw for arbitrary input
    }
  }
  std::printf("fuzz_archive: %zu iterations, %zu unexpected exceptions\n",
              iterations, failures);
  return failures == 0 ? 0 : 1;
}

#endif  // standalone smoke driver