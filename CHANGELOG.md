# Changelog

All notable changes to ZPAQ-NG are documented here. The format follows the
"Keep a Changelog" style; versioning follows a 0/1.x scheme.

## Baseline

The initial state of this repository is a modern C++20 reimplementation of the
ZPAQ archiver, verified byte-for-byte against the original for:
- ZPAQL compiler output (14/14 cases).
- `makeConfig` text and arguments (195/195 cases).
- `compressBlock` output bytes across methods, comments, and checksum modes.
- JIDAC archives created with `-m0..-m5`, `-method s1`, multi-version updates
  with deduplication and deletion, and a 30 MB multi-block single file.
- Cross-list / cross-extract / `-all` / `-until -1` / `-test` against the
  original `zpaq_orig.exe`.

Components present at baseline: ZPAQL compiler and VM, block encode/decode,
arithmetic coding, context models (ICM/CM/MATCH/ISSE/MIX2/MIX/SSE), SHA-1,
CRC-32, LZ77, E8E9, BWT, `makeConfig`, `compressBlock`, JIDAC archive layer,
deduplication, index, rollback, extraction, listing, CLI (`a`/`x`/`l`), CMake,
round-trip tests, and cross-compatibility tests.

## [1.0] - 2026-08-20

### Original ZPAQ compatibility

- Legacy methods `-m0..-m5` and `-method` continue to produce byte-identical
  archives; all baseline tests keep passing.
- NG `create` archives remain readable by the original: the adaptive engine only
  emits valid legacy methods, and the original lists/extracts them with
  byte-identical content.

### New features

- **Next-generation compression engine** (`src/compression_ng`) with adaptive
  strategy selection based on content analysis; `choose_method` only ever
  returns valid legacy method strings.
- **Content analyzer** (`src/analyzer`): deterministic statistical heuristics
  for entropy, byte frequency, runs, repetition, and data classification
  (TEXT/SOURCE_CODE/JSON/XML/CSV/LOG/DATABASE/BINARY/EXECUTABLE/SCIENTIFIC/
  REPETITIVE/RANDOM/MIXED/UNKNOWN).
- **Content-defined chunking** (`src/chunking`): rolling-hash based block
  boundaries.
- **New match finders** (`src/matching`): hash-chain and rolling-hash finders
  behind a common interface, benchmarked against the legacy LZ77.
- **Hardware abstraction** (`src/hardware`): CPU cores/threads, SIMD feature
  detection, RAM detection, `devices` command.
- **SIMD kernels** (`src/simd`): checksum/verify kernels with runtime CPU
  dispatch (SSE2/AVX2) and scalar fallback.
- **Thread pool and parallel block compression** (`src/threading`): `--threads N`
  with deterministic ordered output.
- **Benchmark engine** (`src/benchmark`): `benchmark` command measuring ratio,
  compression/decompression time and throughput; text and JSON output; optional
  level comparison (`--compare`).
- **Recovery** (`src/recovery`): per-block decoding of streaming archives,
  SHA-1 verification, corruption detection and resynchronization; only fully
  validated blocks are written.
- **Modern CLI**: `create`, `benchmark`, `devices`, `info`, `profile`,
  `recover` subcommands alongside the legacy `a`/`x`/`l`; `test` maps to the
  legacy `x -test`. Options: `--level`, `--threads`, `--device`, `--memory`,
  `--dictionary`, `--chunk-size`, `--dedup`, `--verify`, `--no-verify`,
  `--progress`, `--json`, `--verbose`, `--deterministic`, `--compare`.

### Performance

- Incompressible data fast path: analysis detects near-random input and stores
  it without wasting cycles on modeling (measured ~70 MB/s stored vs ~7-13 MB/s
  modeled on random data).
- Parallel multi-block compression with per-worker buffers and ordered writes.

### Testing & tooling

- `tests/recovery_test.cpp`: in-memory archive built with the NG engine;
  intact archives recover 100% byte-exact, and a corrupted block is dropped
  while all valid blocks are preserved.
- `tests/fuzz_archive_smoke`: the archive/block decoders must never crash or
  throw on arbitrary random/truncated input.
- `tests/compat.cmake`: cross-compatibility suite (cross `x -test`, cross
  extraction, byte-identical archives) runnable against the original binary via
  `ZPAQ_ORIG`.
- `.github/workflows/ci.yml`: build & test on Linux/Windows (gcc/clang),
  compatibility, ASan/UBSan sanitizers, libFuzzer, clang-format, manual
  benchmark dispatch.

### Bug fixes

- Recovery (`recover`): the decoder's read-ahead previously skipped block
  boundaries, dropping whole blocks; blocks are now decoded from isolated
  `[tag, next_tag)` slices, so intact archives recover 100% of their data.
- Recovery: only fully verified blocks are written out; corrupted blocks are
  detected via SHA-1 and skipped, with resynchronized later blocks reported as
  "recovered".
- `profile`: match-finder evaluation now caps chain depth and match length, and
  uses a 1 MiB sample, so pathological inputs no longer stall the command; the
  corpus sweep accepts a `quick` 1 MiB mode.
- CMake: `-DSANITIZE=...` flags now apply to every target (previously they were
  added after the targets existed and silently had no effect).
- `tests/compat.cmake`: replaced the nonexistent original `t` command with
  `x -test`, use relative archive paths, and generate binary test data without
  a null byte (cmake content strings cannot hold one).

### Security

- Size/recursion limits for malformed archives; a fuzz harness
  (`src/fuzz/fuzz_archive.cpp`) doubles as a libFuzzer entry point and a
  standalone smoke driver; ASan/UBSan build configurations and the `fuzz_smoke`
  regression test are part of the CI suite.

### Documentation

- `docs/architecture.md`: legacy + NG engine layout, module map, pipeline.
- `docs/credits.md`: attribution (Matt Mahoney public domain, Yuta Mori MIT,
  Erick de S.C. Araújo Unlicense).
- `COPYING.md`: full license text now covers the ZPAQ-NG code (Unlicense) in
  addition to the original ZPAQ (public domain) and libdivsufsort (MIT).
- `versions.md`: implementation history and verification matrix.

### Experimental features

- GPU/XPU backends: not implemented in v1.0. Engineered interfaces only; CPU
  is always sufficient. Engaged only if a future workload shows measurable
  benefit (`--device` rejects anything other than `cpu`/`auto` honestly).

## Planned (post-1.0)

- GPU kernels where benchmarks justify them.
- Auto-tuning and `--level auto`.
- Additional entropy coders (ANS) pending measurement.