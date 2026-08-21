# ZPAQ-NG - Next-generation compression engine

**Version 1.00** — A modern, high-performance reimplementation of the
[ZPAQ](http://mattmahoney.net/dc/zpaq.html) journaling archiver.

ZPAQ-NG is a complete C++20 port of the original ZPAQ format and reference
implementation, verified byte-for-byte against `zpaq_orig.exe`, with a new
next-generation compression engine, adaptive strategy selection, multithreading,
SIMD kernels, hardware detection, benchmarking, corruption recovery, and a
modern CLI — all while maintaining full compatibility with existing ZPAQ
archives and the original `zpaq` binary.

---

## Quick Start

### Best compression (recommended)

```
zpaq_ng create backup.zpaq my_folder --level ng9 --threads auto --dedup
```

- **`--level ng9`** — highest compression level (adaptive; near-random data is
  stored raw, text/source/data gets the best modeling).
- **`--threads auto`** — uses all available CPU cores for parallel compression.
- **`--dedup`** — content-defined chunking; identical content across files is
  deduplicated, reducing archive size.

### Fast compression

```
zpaq_ng create backup.zpaq my_folder --level ng1 --threads auto
```

### Extract

```
zpaq_ng x backup.zpaq -to output/ -force
```

### List contents

```
zpaq_ng l backup.zpaq
```

### Verify integrity

```
zpaq_ng x backup.zpaq -test
```

---

## Building

### CMake (recommended)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The binary is at `build/zpaq_ng.exe` (Windows) or `build/zpaq_ng` (Linux).

### Direct compilation

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Isrc \
    -o zpaq_ng.exe tools/zpaq_ng.cpp src/**/*.cpp
```

### Sanitizers (CI / development)

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DSANITIZE=address,undefined -G Ninja
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

## Commands

### Next-Generation (NG) commands

| Command | Description |
|---------|-------------|
| `create` | Compress files into a streaming ZPAQ archive with adaptive strategy. |
| `benchmark` | Measure compression ratio, speed, and throughput on a corpus. |
| `devices` | Report CPU, SIMD features, cores, RAM, and recommended settings. |
| `info` | Archive summary: versions, files, fragments, blocks. |
| `profile` | Quick multi-level profile on a synthetic corpus. |
| `recover` | Extract valid blocks from a corrupted archive. |

### Legacy commands (byte-compatible with original zpaq)

| Command | Description |
|---------|-------------|
| `a` / `add` | Append files to archive (detects changed files). |
| `x` / `extract` | Extract most recent versions of files. |
| `l` / `list` | List or compare external files to archive by dates. |

Both NG and legacy commands share the same binary. Archives created with
`create` are readable by the original `zpaq` and vice versa.

---

## Compression Levels

| Level | Description | Best for |
|-------|-------------|----------|
| `ng0` | Store only (no compression) | Backup / archiving speed. |
| `ng1` | Fast compression, good ratio | General use (default). |
| `ng2` – `ng5` | Increasing compression effort | Text, source code, logs. |
| `ng6` – `ng9` | Maximum modeling depth | Best possible ratio on text. |

**For maximum compression**: use `--level ng9 --threads auto --dedup`.

The engine automatically detects incompressible data (random bytes, already
compressed files, executables) and stores it raw — so even at `ng9`, archive
speed stays high on binary data.

---

## NG Options Reference

| Option | Default | Description |
|--------|---------|-------------|
| `--level ngN` | `ng1` | Compression level 0–9. |
| `--threads N` | `1` | Parallel blocks. `auto` = all cores. |
| `--device auto` | `auto` | Device selection (v1.0 is CPU-only). |
| `--memory 4G` | `512M` | In-memory budget per file. |
| `--dictionary N` | `auto` | Block/dictionary size hint. |
| `--chunk-size N` | `auto` | Content-defined chunk target. |
| `--dedup` | off | Enable content-defined chunking for dedup. |
| `--verify` | on | Write SHA-1 checksums in archive. |
| `--no-verify` | off | Skip SHA-1 (slightly faster, not recommended). |
| `--json` | off | Machine-readable output (benchmark/info/devices). |
| `--verbose` | off | Extra progress and analysis detail. |
| `--deterministic` | off | Ordered output (same result every run). |
| `--compare` | off | Benchmark: compare two configurations. |

---

## Examples

### Compress a folder with maximum compression

```bash
zpaq_ng create photos.zpaq ~/Photos --level ng9 --threads auto --dedup --verbose
```

### Compress a single file (fast)

```bash
zpaq_ng create data.zpaq dataset.csv --level ng1 --threads auto
```

### Benchmark before archiving

```bash
zpaq_ng benchmark --level ng3 --threads auto
```

### Recover from a corrupted archive

```bash
zpaq_ng recover damaged.zpaq recovered.bin
```

### Check hardware capabilities

```bash
zpaq_ng devices
```

### Profile all levels quickly

```bash
zpaq_ng profile
```

---

## Architecture

```
src/
├── archive/          JIDAC journaling archive layer (ported from zpaq.cpp)
├── benchmark/        Synthetic corpus, ratio/time/throughput measurement
├── chunking/         Content-defined chunking (rolling hash)
├── cli/              NG CLI handlers (create, benchmark, devices, info, ...)
├── compression/      ZPAQL compiler, block encoder, LZ77, E8E9, BWT
├── compression_ng/   Adaptive NG engine (strategy selection + parallel)
├── core/             Types, exceptions, concepts
├── decompression/    Block decoder (bit-exact vs. original)
├── entropy/          Arithmetic coder
├── fuzz/             LibFuzzer + standalone smoke driver
├── hardware/         CPU/SIMD/RAM detection
├── integrity/        SHA-1, CRC-32
├── io/               Reader/Writer streams (file, memory, buffer)
├── matching/         Experimental match finders (hash chain, rolling hash)
├── memory/           Aligned arrays, arena
├── prediction/       Context models (ICM/CM/MATCH/ISSE/MIX2/MIX/SSE)
├── recovery/         Per-block corruption detection + partial recovery
├── simd/             SIMD kernels (SSE2/AVX2) with scalar fallback
└── threading/        Thread pool + OrderedCollector
```

---

## Credits

- **ZPAQ format and reference implementation** — Matt Mahoney (public domain).
- **libdivsufsort-lite** — Yuta Mori (MIT License).
- **ZPAQ-NG** — Erick de S.C. Araújo (Unlicense).

See [docs/credits.md](docs/credits.md) for full attribution.
See [COPYING.md](COPYING.md) for complete license texts.

---

## Links

- [ZPAQ format](http://mattmahoney.net/dc/zpaq.html) — Matt Mahoney
- [CHANGELOG.md](CHANGELOG.md) — version history
- [versions.md](versions.md) — implementation details and verification matrix