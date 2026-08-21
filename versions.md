# Versions

Implementation history of **ZPAQ-NG** — a modern (C++20) reimplementation of the
ZPAQ archiver with byte-for-byte binary compatibility with the original `zpaq`.

## v1.0 (2026-08-20) — Complete port, byte-compatible

The first fully functional version. Every component of the original
`libzpaq.cpp` / `zpaq.cpp` was ported into the `zpaq_ng` namespace, replacing
the old `error()` calls with typed exceptions (`format_error`,
`invalid_argument_error`, ...).

### Compression / decompression core

- `src/compression/zpaql_compiler.{hpp,cpp}` — ZPAQL compiler
  (IF/ELSE/DO/WHILE/FOREVER, POST/PCOMP), verified byte-for-byte (14/14 cases).
- `src/compression/zpaql_vm.cpp`, `zpaql_exec.cpp` — ZPAQL virtual machine
  (interpreter and JIT-safe execution).
- `src/compression/block_encoder.{hpp,cpp}` — block encoder (header, segments,
  SHA-1 checksum, `end_segment_checksum`).
- `src/decompression/block_decoder.{hpp,cpp}` — decoder, bit-exact vs. the
  original (16/16 segments).
- `src/entropy/arithmetic.{hpp,cpp}` — arithmetic coder/decoder.
- `src/prediction/{state_table,predictor,predictor_model}.{hpp,cpp}` — context
  models (ICM/CM/MATCH/ISSE/MIX2/MIX/SSE).
- `src/integrity/{sha1,crc32}.{hpp,cpp}` — SHA-1 and CRC-32.
- `src/compression/post_processor.{hpp,cpp}` — ZPAQL postprocessing.

### Compression methods and preprocessing

- `src/compression/make_config.{hpp,cpp}` — complete `makeConfig` port
  (levels 0..5, LZ77 lazy2/byte/BWT/E8E9, context-model generation).
- `src/compression/compress_block.{hpp,cpp}` — `compressBlock` port (digit
  method expansion, single-block compression with verification).
- `src/compression/lz77.{hpp,cpp}` — `LZBuffer`, `e8e9`, `itos/lg/nbits`.
- `src/compression/divsufsort.hpp` — suffix-array (verbatim, verified).

### JIDAC archive layer

- `src/archive/jidac.{hpp,cpp}` — transactional archiver: `FileIn`/`FileOut`
  (seekable FILE*), reading (`read_archive`) and writing (`add`) of the `c`
  (transaction), `d` (fragments, SHA-1 deduplication), `h` (fragment table) and
  `i` (file index) blocks, extraction (`extract`, single-threaded), listing
  (`list`), `-all`, `-until`/rollback, `scandir`/`makepath`.

### Command-line interface

- `tools/zpaq_ng.cpp` — `a`/`x`/`l` commands with the original zpaq options:
  `-all`, `-f/-force`, `-fragment`, `-method`/`-m`, `-noattributes`, `-not`,
  `-only`, `-s/-summary`, `-test`, `-to`, `-until`. Normalizes `\`→`/` in
  arguments, like the original `wtou()`.

### Build

- `CMakeLists.txt` — static libraries `zpaq_ng_core` and `zpaq_ng_archive`, the
  `zpaq_ng` executable, and CTest tests `roundtrip`, `recovery_test` and
  `fuzz_smoke`.
- `tests/{CMakeLists.txt,roundtrip.cmake}` — round-trip test (add → extract →
  byte-for-byte comparison).
- `tests/recovery_test.cpp` — recovery test (an intact archive recovers 100%;
  a corrupted block is detected and only validated blocks are written).
- `tests/compat.cmake` — cross-compatibility suite against the original binary
  (`ZPAQ_ORIG`): cross `x -test`, cross extraction, and byte-identical archives.
- Direct build: `g++ -std=c++20 -O2 -Wall -Wextra -Isrc -o zpaq_ng.exe
  tools/zpaq_ng.cpp src/**/*.cpp`
- CMake build: `cmake -S . -B build && cmake --build build --parallel`
- Sanitizers: `cmake -S . -B build-asan -DSANITIZE=address,undefined` (the flags
  are applied before the targets are created so every target is instrumented;
  requires the libasan/libubsan runtime, available on CI).
- Fuzzing: `src/fuzz/fuzz_archive.cpp` compiles either as a libFuzzer entry
  point (`-DSANITIZE=fuzzer`) or as a standalone smoke driver; `fuzz_smoke` runs
  in CTest.

### v1.0 NG evolution

- `src/hardware/` — CPU/SIMD/RAM detection and the `devices` command.
- `src/simd/` — checksum kernels with runtime dispatch (SSE2/AVX2) and scalar
  fallback.
- `src/analyzer/` — deterministic statistical analysis and content
  classification (TEXT..RANDOM), incompressible-data detection.
- `src/chunking/` — content-defined chunking (rolling hash).
- `src/threading/` — thread pool and `OrderedCollector` (deterministic ordered
  output).
- `src/compression_ng/` — adaptive engine: it only ever chooses valid legacy
  methods; random data takes a fast raw-storage path.
- `src/benchmark/` — deterministic synthetic corpus (12 types), ratio/time/
  throughput measurement, text/JSON reports.
- `src/matching/` — experimental match finders (hash chain, rolling hash) with
  evaluation bounded by depth/length.
- `src/recovery/` — per-block recovery (each block decoded from its own
  `[tag, next_tag)` slice), SHA-1 verification, ok/corrupted/recovered report.
- New commands: `create`, `benchmark`, `devices`, `info`, `profile`, `recover`;
  options `--level`, `--threads`, `--device`, `--memory`, `--dictionary`,
  `--chunk-size`, `--dedup`, `--verify`, `--progress`, `--json`, `--verbose`,
  `--deterministic`.
- Documentation: `docs/architecture.md`, `docs/credits.md`, `COPYING.md`,
  `CHANGELOG.md`, `.github/workflows/ci.yml` (build/test, compatibility,
  sanitizers, fuzzing, format, manual benchmarks).

### Verification (vs. `zpaq_orig.exe`)

| Test | Result |
| --- | --- |
| ZPAQL compiler (hcomp+pcomp) | byte-identical (14/14) |
| `makeConfig` text + arguments | 195/195 cases |
| `compressBlock` bytes (methods 0-9, x-methods, dosha1, comments) | 195/195 cases |
| Archives created with `-m0..-m5` and `-method s1` | **byte-identical** |
| Multi-version update (dedupe + deletion) | **byte-identical** |
| 30 MB file (425 fragments, multiple blocks) | **byte-identical** |
| `zpaq_ng` reads/extracts original archives and vice versa | OK |
| `-all` extraction (all versions) | identical content |
| `-until -1` (rollback) | same listing on both |
| Cross `-test` | OK |
| `-m0..-m5` (NG via `create`/legacy) vs. original | **byte-identical** |
| NG `create` → list/extract on the original (`zpaq_orig x -test`) | OK, identical content |
| `recover` on an intact archive | 100% blocks OK, identical bytes |
| `recover` with a corrupted block | block detected and skipped; valid blocks preserved |

### Fixes landed during v1.0

- **Recovery**: the decoder's read-ahead skipped block boundaries; blocks are
  now decoded from isolated slices, so intact archives recover 100% of their
  data and corrupted blocks are detected via SHA-1 and skipped.
- **CMake sanitizers**: `-DSANITIZE` flags are now applied before target
  creation (previously added after targets existed, so they had no effect).
- **compat.cmake**: uses the original `x -test` (the original has no `t`
  command), relative archive paths, and binary test data without a null byte.
- **profile**: match-finder evaluation caps chain depth and match length, and
  the corpus sweep supports a quick 1 MiB mode; no longer stalls on
  pathological inputs.
- **COPYING.md**: now covers the ZPAQ-NG code (Unlicense) in addition to the
  original ZPAQ (public domain) and libdivsufsort (MIT).