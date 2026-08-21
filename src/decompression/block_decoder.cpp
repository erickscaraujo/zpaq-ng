// block_decoder.cpp - Segment and block decoder.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "decompression/block_decoder.hpp"

namespace zpaq_ng::decompression {

// ------------------------ SegmentDecoder ------------------------

void SegmentDecoder::init() {
  pr_.init();
  dec_.init(pr_.is_modeled());
}

// Return the next decoded byte or -1 at end of segment.
int SegmentDecoder::decompress() {
  if (pr_.is_modeled()) {  // n > 0 components?
    if (dec_.curr_state() == 0) {  // segment initialization
      for (int i = 0; i < 4; ++i)
        dec_.set_curr((dec_.curr_state() << 8) | static_cast<u32>(dec_.get()));
    }
    if (dec_.decode(0)) {  // 1 = end of segment marker
      if (dec_.curr_state() != 0) throw format_error("decoding end of stream");
      return -1;
    } else {
      int c = 1;
      while (c < 256) {  // get 8 bits
        const int p = pr_.predict() * 2 + 1;
        c += c + dec_.decode(static_cast<u32>(p));
        pr_.update(c & 1);
      }
      return c - 256;
    }
  } else {
    return dec_.store_get();
  }
}

// ------------------------- BlockDecoder -------------------------

// Find the start of a block and return true if found.
bool BlockDecoder::find_block() {
  if (state_ != BLOCK) throw invalid_argument_error("find_block called in wrong state");

  // Rolling hashes initialized to the state reached after the 13 byte tag
  // so that the match fires once the tag and "zPQ" (16 bytes) are consumed.
  u32 h1 = 0x3D49B113, h2 = 0x29EB7F93, h3 = 0x2614BE13, h4 = 0x3828EB13;
  int c;
  while ((c = dec_.get()) != -1) {
    h1 = h1 * 12 + static_cast<u32>(c);
    h2 = h2 * 20 + static_cast<u32>(c);
    h3 = h3 * 28 + static_cast<u32>(c);
    h4 = h4 * 44 + static_cast<u32>(c);
    if (h1 == 0xB16B88F1 && h2 == 0xFF5376F1 && h3 == 0x72AC5BF1 &&
        h4 == 0x2F909AF1)
      break;  // hash of the 16 byte string tag + "zPQ"
  }
  if (c == -1) return false;

  // Read header
  if ((c = dec_.get()) != 1 && c != 2)
    throw format_error("unsupported ZPAQ level");
  if (dec_.get() != 1) throw format_error("unsupported ZPAQL type");
  z_.read_header(&dec_.raw());
  if (c == 1 && z_.header_size() > 6 && z_.num_components() == 0)
    throw format_error("ZPAQ level 1 requires at least 1 component");
  state_ = FILENAME;
  decode_state_ = FIRSTSEG;
  return true;
}

// Read the start of a segment (1) or end of block code (255).
// If a segment is found, write the filename and return true, else false.
bool BlockDecoder::find_filename(io::Writer* filename) {
  if (state_ != FILENAME) throw invalid_argument_error("find_filename in wrong state");
  const int c = dec_.get();
  if (c == 1) {  // segment found
    while (true) {
      const int d = dec_.get();
      if (d == -1) throw format_error("unexpected EOF");
      if (d == 0) {
        state_ = COMMENT;
        return true;
      }
      if (filename) filename->put(static_cast<char>(d));
    }
  } else if (c == 255) {  // end of block found
    state_ = BLOCK;
    return false;
  } else {
    throw format_error("missing segment or end of block");
  }
}

// Read the comment from the segment header.
void BlockDecoder::read_comment(io::Writer* comment) {
  if (state_ != COMMENT) throw invalid_argument_error("read_comment in wrong state");
  state_ = DATA;
  while (true) {
    const int c = dec_.get();
    if (c == -1) throw format_error("unexpected EOF");
    if (c == 0) break;
    if (comment) comment->put(static_cast<char>(c));
  }
  if (dec_.get() != 0) throw format_error("missing reserved byte");
}

// Decompress n bytes, or all if n < 0. Return false if done.
bool BlockDecoder::decompress(int64_t n) {
  if (state_ != DATA) throw invalid_argument_error("decompress in wrong state");
  if (decode_state_ == SKIP)
    throw format_error("decompression after skipped segment");

  // Initialize models to start decompressing block
  if (decode_state_ == FIRSTSEG) {
    dec_.init();
    if (z_.header_size() <= 5) throw format_error("missing block header");
    pp_.init(z_.header()[4], z_.header()[5]);
    decode_state_ = SEG;
  }

  // Decompress and load PCOMP into postprocessor
  while ((pp_.get_state() & 3) != 1) pp_.write(dec_.decompress());

  // Decompress n bytes, or all if n < 0
  while (n != 0) {
    const int c = dec_.decompress();
    pp_.write(c);
    if (c == -1) {
      state_ = SEGEND;
      return false;
    }
    if (n > 0) --n;
  }
  return true;
}

// Read end of block. If a SHA1 checksum is present, write 1 and the
// 20 byte checksum into sha1string, else write 0 in first byte.
// If sha1string is 0 then discard it.
void BlockDecoder::read_segment_end(char* sha1string) {
  if (state_ != DATA && state_ != SEGEND)
    throw invalid_argument_error("read_segment_end in wrong state");

  // Skip remaining data if any and get next byte
  int c = 0;
  if (state_ == DATA) {
    c = dec_.skip();
    decode_state_ = SKIP;
  } else if (state_ == SEGEND) {
    c = dec_.get();
  }
  state_ = FILENAME;

  // Read checksum
  if (c == 254) {
    if (sha1string) sha1string[0] = 0;
  } else if (c == 253) {
    if (sha1string) {
      sha1string[0] = 1;
      for (int i = 1; i <= 20; ++i) sha1string[i] = static_cast<char>(dec_.get());
    } else {
      for (int i = 0; i < 20; ++i) dec_.get();
    }
  } else {
    throw format_error("missing end of segment marker");
  }
}

} // namespace zpaq_ng::decompression