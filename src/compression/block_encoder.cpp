// block_encoder.cpp - Block encoder.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/block_encoder.hpp"

#include <cstring>

#include "compression/zpaql_compiler.hpp"

namespace zpaq_ng::compression {

namespace {
// Read a 16 bit little-endian size (toU16 semantics: unsigned).
int to_u16(const u8* p) { return p[0] + 256 * p[1]; }
} // namespace

// Write the 13 byte start tag.
void BlockEncoder::write_tag() {
  if (state_ != INIT) throw invalid_argument_error("write_tag in wrong state");
  static const u8 tag[13] = {0x37, 0x6B, 0x53, 0x74, 0xA0, 0x31, 0x83,
                             0xD3, 0x8C, 0xB2, 0x28, 0xB0, 0xD3};
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  for (u8 b : tag) out->put(b);
}

// Encode one byte (0..255) or -1 at end of segment.
void BlockEncoder::encode_byte(int c) {
  if (pr_.is_modeled()) {
    if (c == -1) {
      enc_.encode(1, 0);  // end of segment marker
    } else {
      enc_.encode(0, 0);  // start of byte marker
      for (int i = 7; i >= 0; --i) {
        const int p = pr_.predict() * 2 + 1;
        const int y = (c >> i) & 1;
        enc_.encode(y, static_cast<u32>(p));
        pr_.update(y);
      }
    }
  } else {
    enc_.store_put(c);
  }
}

// Write "zPQ" + level + ZPAQL type + the COMP/HCOMP header.
void BlockEncoder::init_block_header() {
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  out->put('z');
  out->put('P');
  out->put('Q');
  out->put(1 + (z_.num_components() == 0));  // level 1 or 2
  out->put(1);                               // ZPAQL type
  z_.write_header(out, false);
}

void BlockEncoder::start_block(int level) {
  // Preset models: min.cfg, mid.cfg, max.cfg. Each entry is a 16 bit
  // little-endian size followed by that many header bytes; 0,0 ends the
  // list. Sizes are read as unsigned (toU16 semantics).
  static const u8 models[] = {
      26, 0, 1, 2, 0, 0, 2, 3, 16, 8, 19, 0, 0, 96, 4, 28,
      59, 10, 59, 112, 25, 10, 59, 10, 59, 112, 56, 0,

      69, 0, 3, 3, 0, 0, 8, 3, 5, 8, 13, 0, 8, 17, 1, 8,
      18, 2, 8, 18, 3, 8, 19, 4, 4, 22, 24, 7, 16, 0, 7, 24,
      0xFF, 0, 17, 104, 74, 4, 95, 1, 59, 112, 10, 25, 59, 112, 10, 25,
      59, 112, 10, 25, 59, 112, 10, 25, 59, 112, 10, 25, 59, 10, 59, 112,
      25, 69, 0xCF, 8, 112, 56, 0,

      0xC4, 0, 5, 9, 0, 0, 22, 1, 0xA0, 3, 5, 8, 13, 1, 8, 16,
      2, 8, 18, 3, 8, 19, 4, 8, 19, 5, 8, 20, 6, 4, 22, 24,
      3, 17, 8, 19, 9, 3, 13, 3, 13, 3, 13, 3, 14, 7, 16, 0,
      15, 24, 0xFF, 7, 8, 0, 16, 10, 0xFF, 6, 0, 15, 16, 24, 0, 9,
      8, 17, 32, 0xFF, 6, 8, 17, 18, 16, 0xFF, 9, 16, 19, 32, 0xFF, 6,
      0, 19, 20, 16, 0, 0, 17, 104, 74, 4, 95, 2, 59, 112, 10, 25,
      59, 112, 10, 25, 59, 112, 10, 25, 59, 112, 10, 25, 59, 112, 10, 25,
      59, 10, 59, 112, 10, 25, 59, 112, 10, 25, 69, 0xB7, 32, 0xEF, 64, 47,
      14, 0xE7, 91, 47, 10, 25, 60, 26, 48, 0x86, 0x97, 20, 112, 63, 9, 70,
      0xDF, 0, 39, 3, 25, 112, 26, 52, 25, 25, 74, 10, 4, 59, 112, 25,
      10, 4, 59, 112, 25, 10, 4, 59, 112, 25, 65, 0x8F, 0xD4, 72, 4, 59,
      112, 8, 0x8F, 0xD8, 8, 68, 0xAF, 60, 60, 25, 69, 0xCF, 9, 112, 25, 25,
      25, 25, 25, 112, 56, 0,

      0, 0};  // 0, 0 = end of list

  if (state_ != INIT) throw invalid_argument_error("start_block in wrong state");
  if (level < 1) throw format_error("compression level must be at least 1");
  const u8* p = models;
  for (int i = 1; i < level && to_u16(p); ++i) p += to_u16(p) + 2;
  if (to_u16(p) < 1) throw format_error("compression level too high");
  start_block_bytes(p);
}

void BlockEncoder::start_block_bytes(const u8* hcomp) {
  if (state_ != INIT) throw invalid_argument_error("start_block_bytes in wrong state");
  const std::size_t size = static_cast<std::size_t>(hcomp[0] + 256 * hcomp[1]) + 2;
  io::MemoryReader m(hcomp, size);
  z_.read_header(&m);
  pz_.sha1 = &sha1_;
  if (z_.header_size() <= 6) throw format_error("missing block header");
  init_block_header();
  state_ = BLOCK1;
}

void BlockEncoder::start_block_store() {
  static const u8 store_header[9] = {7, 0, 0, 0, 0, 0, 0, 0, 0};
  start_block_bytes(store_header);
}

void BlockEncoder::start_block_config(const char* config, const int* args,
                                      io::Writer* pcomp_cmd) {
  if (state_ != INIT) throw invalid_argument_error("start_block in wrong state");
  Compiler compiler(config, args, z_, pz_, pcomp_cmd);
  pz_.sha1 = &sha1_;
  if (z_.header_size() <= 6) throw format_error("missing block header");
  init_block_header();
  state_ = BLOCK1;
}

// Write a segment header.
void BlockEncoder::start_segment(const std::string& filename,
                                 const std::string& comment) {
  if (state_ != BLOCK1 && state_ != BLOCK2)
    throw invalid_argument_error("start_segment in wrong state");
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  out->put(1);
  for (char c : filename) out->put(static_cast<unsigned char>(c));
  out->put(0);
  for (char c : comment) out->put(static_cast<unsigned char>(c));
  out->put(0);
  out->put(0);
  state_ = (state_ == BLOCK1) ? SEG1 : SEG2;
}

// Initialize encoding and write PCOMP to the first segment.
void BlockEncoder::post_process(const u8* pcomp, int len) {
  if (state_ == SEG2) return;
  if (state_ != SEG1) throw invalid_argument_error("post_process in wrong state");
  enc_.init(!pr_.is_modeled());
  pr_.init();
  if (!pcomp) {
    len = pz_.h_end() - pz_.h_begin();
    if (len > 0) pcomp = pz_.header() + pz_.h_begin();
  } else if (len == 0) {
    len = pcomp[0] + 256 * pcomp[1];
    pcomp += 2;
  }
  if (len > 0) {
    encode_byte(1);  // PROG
    encode_byte(len & 255);
    encode_byte((len >> 8) & 255);
    for (int i = 0; i < len; ++i) encode_byte(pcomp[i] & 255);
    if (verify_) pz_.init_p();
  } else {
    encode_byte(0);  // PASS
  }
  state_ = SEG2;
}

// Compress n bytes, or to EOF if n < 0.
bool BlockEncoder::compress(int64_t n) {
  if (state_ == SEG1) post_process(nullptr, 0);
  if (state_ != SEG2) throw invalid_argument_error("compress in wrong state");

  constexpr int BUFSIZE = 1 << 14;
  char buf[BUFSIZE];
  const bool has_pcomp = pz_.h_end() > pz_.h_begin();
  while (n != 0) {
    int nbuf = BUFSIZE;
    if (n >= 0 && n < nbuf) nbuf = static_cast<int>(n);
    const int nr = static_cast<int>(in_ ? in_->read(buf, static_cast<size_t>(nbuf)) : 0);
    if (nr < 0 || nr > BUFSIZE || nr > nbuf)
      throw format_error("invalid read size");
    if (nr <= 0) return false;
    if (n >= 0) n -= nr;
    for (int i = 0; i < nr; ++i) {
      const int ch = static_cast<unsigned char>(buf[i]);
      encode_byte(ch);
      if (verify_) {
        if (has_pcomp) pz_.run(static_cast<u32>(ch));
        else sha1_.put(ch);
      }
    }
  }
  return true;
}

// End segment, write sha1string if present.
void BlockEncoder::end_segment(const char* sha1string) {
  if (state_ == SEG1) post_process(nullptr, 0);
  if (state_ != SEG2) throw invalid_argument_error("end_segment in wrong state");
  encode_byte(-1);
  if (verify_ && pz_.h_end() > pz_.h_begin()) {
    pz_.run(0xFFFFFFFFu);
    pz_.flush();
  }
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  out->put(0);
  out->put(0);
  out->put(0);
  out->put(0);
  if (sha1string) {
    out->put(253);
    for (int i = 0; i < 20; ++i) out->put(sha1string[i] & 255);
  } else {
    out->put(254);
  }
  state_ = BLOCK2;
}

// End segment, write checksum if verify is true.
const u8* BlockEncoder::end_segment_checksum(bool dosha1) {
  if (state_ == SEG1) post_process(nullptr, 0);
  if (state_ != SEG2) throw invalid_argument_error("end_segment_checksum in wrong state");
  encode_byte(-1);
  if (verify_ && pz_.h_end() > pz_.h_begin()) {
    pz_.run(0xFFFFFFFFu);
    pz_.flush();
  }
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  out->put(0);
  out->put(0);
  out->put(0);
  out->put(0);
  if (verify_) {
    const char* r = sha1_.result();
    std::memcpy(sha1result_, r, 20);
  }
  if (verify_ && dosha1) {
    out->put(253);
    for (int i = 0; i < 20; ++i) out->put(sha1result_[i] & 255);
  } else {
    out->put(254);
  }
  state_ = BLOCK2;
  return verify_ ? sha1result_ : nullptr;
}

// End block.
void BlockEncoder::end_block() {
  if (state_ != BLOCK2) throw invalid_argument_error("end_block in wrong state");
  io::Writer* out = enc_.output();
  if (!out) throw format_error("block encoder has no output");
  out->put(255);
  state_ = INIT;
}

} // namespace zpaq_ng::compression