// block_encoder.hpp - Block encoder (Compressor).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Writes ZPAQ level 1/2 blocks: the 13 byte tag, block header (ZPAQL model),
// and one or more segments with optional SHA1 checksums. A segment is either
// range-coded against the model or stored raw (4 byte length + bytes) when
// the model has no components.
//
// Faithful port of libzpaq's Compressor. Blocks produced here are decodable
// by the original zpaq and vice versa.

#ifndef ZPAQ_NG_COMPRESSION_BLOCK_ENCODER_HPP
#define ZPAQ_NG_COMPRESSION_BLOCK_ENCODER_HPP

#include <cstdint>
#include <string>

#include "compression/zpaql_vm.hpp"
#include "entropy/arithmetic.hpp"
#include "integrity/sha1.hpp"
#include "io/streams.hpp"
#include "prediction/predictor.hpp"

namespace zpaq_ng::compression {

class BlockEncoder {
public:
  BlockEncoder() : enc_(), pr_(z_) {}

  // Set the destination byte stream.
  void set_output(io::Writer* out) { enc_.set_output(out); }

  // Write the 13 byte start tag. Call once before start_block.
  void write_tag();

  // Start a block using a preset model: level 1 (min), 2 (mid), 3 (max).
  // Throws for level < 1 or > 3.
  void start_block(int level);

  // Start a block from raw ZPAQL byte code: hsize little-endian followed by
  // the header body.
  void start_block_bytes(const u8* hcomp);

  // Start a block by compiling a ZPAQL config string (COMP+HCOMP, and PCOMP
  // when present). args substitutes $1..$9; pcomp_cmd receives the PCOMP
  // command text up to ';' when non-null.
  void start_block_config(const char* config, const int* args = nullptr,
                          io::Writer* pcomp_cmd = nullptr);

  // Start a block with no model (store mode, like zpaq method "0").
  void start_block_store();

  // When set, feed input bytes through PCOMP (if any) or SHA1 so that
  // end_segment_checksum can report the checksum.
  void set_verify(bool v) { verify_ = v; }

  // Write the COMP+HCOMP (false) or PCOMP (true) sections.
  void hcomp(io::Writer* out) { z_.write_header(out, false); }
  bool pcomp(io::Writer* out) { return pz_.write_header(out, true); }

  // Write a segment header (type byte 1, filename, comment).
  void start_segment(const std::string& filename,
                     const std::string& comment);

  // Set the input for compress().
  void set_input(io::Reader* in) { in_ = in; }

  // Initialize encoding and write PCOMP to the first segment. If pcomp is
  // null the PCOMP from pz_ is used; if len is 0 the length is read from
  // pcomp[0..1]. A null pz_ writes no postprocessor (PASS).
  void post_process(const u8* pcomp, int len);

  // Compress n bytes from the input, or all if n < 0. Returns false at EOF.
  bool compress(int64_t n);

  // End the segment. If sha1string is non-null, writes 253 + 20 bytes,
  // otherwise 254 (no checksum).
  void end_segment(const char* sha1string);

  // End the segment, writing the checksum when dosha1 and verify are set.
  // Returns the 20 byte checksum, or null when not verified.
  const u8* end_segment_checksum(bool dosha1);

  // End the block (writes 255).
  void end_block();

  // Per-component prediction (diagnostics).
  int stat(int x) { return pr_.stat(x); }

private:
  enum State { INIT, BLOCK1, SEG1, SEG2, BLOCK2 };

  void init_block_header();  // write "zPQ" + level + type + header
  void encode_byte(int c);   // modeled or store, through enc_ + pr_

  bool verify_ = false;
  io::Reader* in_ = nullptr;
  ZPAQL z_, pz_;
  entropy::ArithmeticEncoder enc_;
  prediction::Predictor pr_;
  integrity::SHA1 sha1_;
  u8 sha1result_[20];
  State state_ = INIT;
};

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_BLOCK_ENCODER_HPP