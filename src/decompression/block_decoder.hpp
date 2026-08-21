// block_decoder.hpp - Segment and block decoder (Decoder/Decompresser).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// SegmentDecoder decodes one arithmetic-coded or stored segment: it drives
// the range decoder with the predictor probabilities exactly like libzpaq's
// Decoder. BlockDecoder parses a whole block: it locates the "zPQ" tag,
// reads the block header (ZPAQL model), iterates over segments, routes
// decoded bytes through the PostProcessor, and reads the segment-end
// checksum marker.
//
// Both are faithful ports. Every block written by the original zpaq -m0..5
// must decode here and every block produced by BlockEncoder must be
// accepted by the original.

#ifndef ZPAQ_NG_DECOMPRESSION_BLOCK_DECODER_HPP
#define ZPAQ_NG_DECOMPRESSION_BLOCK_DECODER_HPP

#include <cstdint>

#include "compression/post_processor.hpp"
#include "compression/zpaql_vm.hpp"
#include "entropy/arithmetic.hpp"
#include "integrity/sha1.hpp"
#include "io/streams.hpp"
#include "prediction/predictor.hpp"

namespace zpaq_ng::decompression {

// Decodes bytes out of one segment using the range coder and predictor.
class SegmentDecoder {
public:
  explicit SegmentDecoder(compression::ZPAQL& z) : pr_(z) {}

  void set_input(io::Reader* in) { dec_.set_input(in); }

  // Initialize the range coder and predictor at the start of a block.
  void init();

  // Return the next decoded byte or -1 at end of segment.
  int decompress();

  // Skip to the end of the segment and return the next byte.
  int skip() { return dec_.skip(pr_.is_modeled()); }

  // Read one raw input byte (segment headers, block tags).
  int get() { return dec_.get(); }

  // Per-component prediction (diagnostics).
  int stat(int x) { return pr_.stat(x); }

  // The underlying range decoder as an io::Reader.
  entropy::ArithmeticDecoder& raw() { return dec_; }

  // How many bytes the decoder has read ahead of the current position.
  int buffered() const { return dec_.buffered(); }

private:
  entropy::ArithmeticDecoder dec_;
  prediction::Predictor pr_;
};

// Parses a ZPAQ block: header, segments, and checksums.
class BlockDecoder {
public:
  BlockDecoder() : dec_(z_) {}

  void set_input(io::Reader* in) { dec_.set_input(in); }

  // Adapter so ZPAQL::read_header can consume raw input bytes.
  entropy::ArithmeticDecoder& raw() { return dec_.raw(); }

  // Scan for and parse the start of a block. Returns true if a block was
  // found, false at end of input. Throws on malformed input.
  bool find_block();

  // Write the COMP/HCOMP sections to out (diagnostics).
  void hcomp(io::Writer* out) { z_.write_header(out, false); }

  // Read the start of a segment; write the filename to out (or discard).
  // Returns true if a segment was found, false at end of block.
  bool find_filename(io::Writer* filename = nullptr);

  // Read the comment from the segment header.
  void read_comment(io::Writer* comment = nullptr);

  // Route postprocessor output.
  void set_output(io::Writer* out) { pp_.set_output(out); }
  void set_sha1(integrity::SHA1* sha1ptr) { pp_.set_sha1(sha1ptr); }

  // Decompress n bytes (or all if n < 0), feeding them through the
  // postprocessor. Returns false when the segment ends.
  bool decompress(int64_t n = -1);

  // Write the PCOMP header+program to out; returns false if none.
  bool pcomp(io::Writer* out) { return pp_.z.write_header(out, true); }

  // Read the end of the segment. If a SHA1 checksum is present, writes 1
  // followed by the 20 byte checksum into sha1string; otherwise writes 0.
  // If sha1string is null the checksum is discarded.
  void read_segment_end(char* sha1string = nullptr);

  // Per-component prediction (diagnostics).
  int stat(int x) { return dec_.stat(x); }

  // How many bytes read ahead.
  int buffered() const { return dec_.buffered(); }

private:
  enum State { BLOCK, FILENAME, COMMENT, DATA, SEGEND };
  enum DecodeState { FIRSTSEG, SEG, SKIP };

  compression::ZPAQL z_;
  SegmentDecoder dec_;
  compression::PostProcessor pp_;
  State state_ = BLOCK;
  DecodeState decode_state_ = FIRSTSEG;
};

} // namespace zpaq_ng::decompression

#endif // ZPAQ_NG_DECOMPRESSION_BLOCK_DECODER_HPP