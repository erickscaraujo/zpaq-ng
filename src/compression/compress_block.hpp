// compress_block.hpp - Single-block compressor.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Faithful port of libzpaq::compressBlock (libzpaq.cpp 7537-7731). Compresses
// an in-memory buffer into one segment in one block, choosing the method from
// the method string ("0".."9", "x...", "s", "i"). The input buffer is modified
// in place by the E8E9 transform when the method requests it.

#ifndef ZPAQ_NG_COMPRESSION_COMPRESS_BLOCK_HPP
#define ZPAQ_NG_COMPRESSION_COMPRESS_BLOCK_HPP

#include <cstddef>

#include "io/streams.hpp"

namespace zpaq_ng::compression {

// Compress in to out in 1 segment in 1 block using the algorithm described
// in method. If method begins with a digit then choose a method depending on
// type. filename and comment are saved in the segment header. The comment is
// the input size as a decimal string, plus " " + comment when comment is
// non-null (the " jDC\x01" journaling marker is added by the archive layer).
void compress_block(io::Buffer* in, io::Writer* out, const char* method_,
                    const char* filename, const char* comment, bool dosha1);

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_COMPRESS_BLOCK_HPP