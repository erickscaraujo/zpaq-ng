# Credits

## ZPAQ-NG v1.0

**Developed and engineered by Erick de S.C. Araújo.**

ZPAQ-NG is a modern (C++20/C++23) reimplementation and evolution of the ZPAQ
archiver. It preserves byte-for-byte compatibility with the original ZPAQ format
while adding a new next-generation compression engine, hardware-aware scheduling,
multithreading, SIMD kernels, adaptive compression, benchmarking, and a modern
CLI.

All new code in this project — architecture, refactoring, new compression
components, optimizations, tooling, and infrastructure — is the work of
Erick de S.C. Araújo and is distributed under the [Unlicense](https://unlicense.org)
unless a file states otherwise.

## ZPAQ original — Matt Mahoney

The ZPAQ format and the reference implementation (`zpaq.cpp`, `libzpaq.cpp`,
`libzpaq.h`) were created by **Matt Mahoney** and are placed in the public
domain. ZPAQ-NG is a faithful, verified reimplementation of that work:

- `src/compression/*` (block encoder, config compiler, LZ77/BWT preprocessing,
  postprocessing, `makeConfig`, `compressBlock`) is a port of `libzpaq.cpp`.
- `src/decompression/block_decoder.*` is a port of the libzpaq `Decompresser`.
- `src/entropy/arithmetic.*` is a port of the libzpaq arithmetic coder.
- `src/prediction/*` (state table, predictor, ICM/CM/MATCH/ISSE/MIX2/MIX/SSE
  models) is a port of the libzpaq context models.
- `src/integrity/sha1.*`, `crc32.*` are ports of the libzpaq checksums.
- `src/archive/jidac.*` is a port of the JIDAC journaling archive layer
  (`zpaq.cpp`), including fragment deduplication by SHA-1, the index, rollback,
  extraction, and listing.

The original project home is <http://mattmahoney.net/dc/zpaq.html>. The full
public-domain text that applies to the original code is reproduced in
[COPYING.md](../COPYING.md).

## libdivsufsort — Yuta Mori

The suffix-array construction code used for the BWT/LZ77-SA preprocessing is
`libdivsufsort-lite` version 2.00 by **Yuta Mori** (Copyright (c) 2003-2008
Yuta Mori, All Rights Reserved), licensed under the MIT License. It is included
verbatim in `src/compression/divsufsort.hpp` with its original notice and the
full MIT license text, and is also reproduced in [COPYING.md](../COPYING.md).

## Third-party components

| Component | Author | License | Location |
| --- | --- | --- | --- |
| ZPAQ reference implementation | Matt Mahoney | Public Domain (Unlicense) | ported to `src/compression`, `src/decompression`, `src/entropy`, `src/prediction`, `src/integrity`, `src/archive` |
| libdivsufsort-lite 2.00 | Yuta Mori | MIT | `src/compression/divsufsort.hpp` |

## Contributions of the ZPAQ-NG implementation

- Modern C++20/23 codebase with type-safe exceptions, RAII, and `std::span`.
- Byte-for-byte verification against the original binaries.
- Next-generation compression engine with adaptive strategy selection.
- Content analysis and classification heuristics.
- Content-defined chunking.
- Thread pool and parallel block compression.
- SIMD kernels with runtime CPU dispatch and scalar fallback.
- Hardware abstraction and auto-selection (`devices`, `--device auto`).
- Benchmark and profiling tools.
- Fuzzing, sanitizer, and static-analysis configuration.
- Modern CLI with subcommands and JSON output.

## License notices are preserved

No license notice, copyright line, or attribution present in the original ZPAQ
or in the baseline implementation has been removed or altered. New ZPAQ-NG
files carry the Unlicense notice and this document together form the complete
attribution of the project.