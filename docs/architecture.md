# Architecture

ZPAQ-NG v1.0 is organized as a layered, modular C++20 codebase. Two coexisting
engines share one archive layer:

```
ZPAQ-NG
│
├── Legacy Compatibility Engine   (src/compression, src/decompression, ...)
│
└── Next Generation Compression Engine  (src/compression_ng, ...)
```

Both write/read the same ZPAQ container via the shared archive and block layers.
The legacy engine preserves the exact bytes and behavior of the original ZPAQ;
the NG engine is a separate pipeline for new algorithms.

## Directory map

| Directory | Responsibility |
| --- | --- |
| `src/core` | Types, errors, concepts. No dependencies on other modules. |
| `src/io` | Reader/Writer stream abstraction (`FileIn`/`FileOut` live in archive). |
| `src/entropy` | Arithmetic coder (ZPAQ-compatible) + entropy coder interface. |
| `src/prediction` | Context-mixing bit predictors (ICM/CM/MATCH/ISSE/MIX2/MIX/SSE). |
| `src/integrity` | SHA-1, SHA-256, CRC-32, IntegrityProvider interface. |
| `src/compression` | Legacy block encoder, ZPAQL compiler/VM, LZ77/BWT, `makeConfig`, `compressBlock`, divsufsort. |
| `src/decompression` | Legacy block/segment decoder. |
| `src/archive` | JIDAC journaling archive: add/extract/list, dedup, index, rollback. |
| `src/compression_ng` | Next-generation engine: adaptive strategy selection. |
| `src/analyzer` | Content analyzer and classification. |
| `src/chunking` | Content-defined chunking (rolling hash). |
| `src/matching` | New match finders (hash chain, rolling hash). |
| `src/threading` | Thread pool, work queue, ordered writer. |
| `src/memory` | Arenas, aligned buffers, memory limits. |
| `src/simd` | SIMD kernels with runtime dispatch and scalar fallback. |
| `src/hardware` | CPU/GPU/XPU detection and device model. |
| `src/scheduling` | Hardware-aware task scheduler. |
| `src/benchmark` | Benchmark engine (ratio, speeds, thread scaling, JSON). |
| `src/diagnostics` | Profiling and logging hooks. |
| `src/recovery` | Corruption detection and partial-recovery reporting. |
| `src/compatibility` | Cross-version verification helpers. |
| `src/cli` | Command-line front-end. |

Modules are acyclic: `core` → `io`/`memory` → everything else.

## The NG compression pipeline

```
Input
  ↓
File Analyzer ────────→ classification + stats
  ↓
Adaptive Chunker ──────→ content-defined blocks
  ↓
Strategy Selector ─────→ {method, block size, threads, device, model set}
  ↓
Preprocessor / Transform
  ↓
Match Finder / Dictionary
  ↓
Prediction / Context Modeling
  ↓
Entropy Coding
  ↓
Integrity
  ↓
Compressed Block
```

Each stage has a narrow interface so new algorithms can be introduced without
touching the rest of the system. Selection is always driven by measurement
(see `docs/benchmarking.md`), never by fashion.

## Hardware-aware scheduling

```
                Scheduler
                   │
     ┌─────────────┼─────────────┐
     ↓             ↓             ↓
    CPU           GPU           XPU        (GPU/XPU only when measurable)
     │             │             │
   SIMD          CUDA          SYCL
     └─────────────┼─────────────┘
                   ↓
            Compression Tasks
```

CPU is always available. GPU/XPU backends are optional and only engaged when
measured end-to-end benefit exceeds transfer and orchestration cost.

## Threading model

Independent blocks are compressed in parallel by a worker pool; results are
written in deterministic order by an ordered writer. `--threads N` controls the
pool size, `--deterministic` forces identical output regardless of thread count.

## Compatibility contract

- Legacy `-m0..-m5`, `-method`, streaming `s` and journaling `x` remain byte
  identical to the original ZPAQ for identical input and date.
- Archives written by the original tool are readable, extractable, and testable
  by ZPAQ-NG and vice versa.
- Format changes are additive and gated behind NG methods; files are never
  declared original-compatible unless they truly are.

See `docs/compatibility.md`, `docs/format.md`, and `docs/performance.md`.