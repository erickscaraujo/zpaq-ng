// compress_block.cpp - Single-block compressor.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/compress_block.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <string>

#include "compression/block_encoder.hpp"
#include "compression/lz77.hpp"
#include "compression/make_config.hpp"
#include "core/error.hpp"
#include "integrity/sha1.hpp"

namespace zpaq_ng::compression {

void compress_block(io::Buffer* in, io::Writer* out, const char* method_,
                    const char* filename, const char* comment, bool dosha1) {
  assert(in);
  assert(out);
  assert(method_);
  assert(method_[0]);
  std::string method = method_;
  const unsigned n = static_cast<unsigned>(in->size());  // input size
  const int arg0 = std::max(lg(n + 4095) - 20, 0);       // block size
  assert((1u << (arg0 + 20)) >= n + 4096);

  // Get type from method "LB,R,t" where L is level 0..5, B is block
  // size 0..11, R is redundancy 0..255, t = 0..3 = binary, text, exe, both.
  unsigned type = 0;
  if (std::isdigit(method[0])) {
    int commas = 0, arg[4] = {0};
    for (int i = 1; i < int(method.size()) && commas < 4; ++i) {
      if (method[i] == ',' || method[i] == '.') ++commas;
      else if (std::isdigit(method[i])) arg[commas] = arg[commas] * 10 + method[i] - '0';
    }
    if (commas == 0) type = 512;
    else type = arg[1] * 4 + arg[2];
  }

  // Expand default methods
  if (std::isdigit(method[0])) {
    const int level = method[0] - '0';
    assert(level >= 0 && level <= 9);

    // build models
    const int doe8 = (type & 2) * 2;
    method = "x" + itos(arg0);
    std::string htsz = "," + itos(19 + arg0 + (arg0 <= 6));  // lz77 hash table size
    std::string sasz = "," + itos(21 + arg0);                // lz77 suffix array size

    // store uncompressed
    if (level == 0)
      method = "0" + itos(arg0) + ",0";

    // LZ77, no model. Store if hard to compress
    else if (level == 1) {
      if (type < 40) method += ",0";
      else {
        method += "," + itos(1 + doe8) + ",";
        if (type < 80) method += "4,0,1,15";
        else if (type < 128) method += "4,0,2,16";
        else if (type < 256) method += "4,0,2" + htsz;
        else if (type < 960) method += "5,0,3" + htsz;
        else method += "6,0,3" + htsz;
      }
    }

    // LZ77 with longer search
    else if (level == 2) {
      if (type < 32) method += ",0";
      else {
        method += "," + itos(1 + doe8) + ",";
        if (type < 64) method += "4,0,3" + htsz;
        else method += "4,0,7" + sasz + ",1";
      }
    }

    // LZ77 with CM depending on redundancy
    else if (level == 3) {
      if (type < 20)  // store if not compressible
        method += ",0";
      else if (type < 48)  // fast LZ77 if barely compressible
        method += "," + itos(1 + doe8) + ",4,0,3" + htsz;
      else if (type >= 640 || (type & 1))  // BWT if text or highly compressible
        method += "," + itos(3 + doe8) + "ci1";
      else  // LZ77 with O0-1 compression of up to 12 literals
        method += "," + itos(2 + doe8) + ",12,0,7" + sasz + ",1c0,0,511i2";
    }

    // LZ77+CM, fast CM, or BWT depending on type
    else if (level == 4) {
      if (type < 12)
        method += ",0";
      else if (type < 24)
        method += "," + itos(1 + doe8) + ",4,0,3" + htsz;
      else if (type < 48)
        method += "," + itos(2 + doe8) + ",5,0,7" + sasz + "1c0,0,511";
      else if (type < 900) {
        method += "," + itos(doe8) + "ci1,1,1,1,2a";
        if (type & 1) method += "w";
        method += "m";
      } else
        method += "," + itos(3 + doe8) + "ci1";
    }

    // Slow CM with lots of models
    else {  // 5..9

      // Model text files
      method += "," + itos(doe8);
      if (type & 1) method += "w2c0,1010,255i1";
      else method += "w1i1";
      method += "c256ci1,1,1,1,1,1,2a";

      // Analyze the data
      const int NR = 1 << 12;
      int pt[256] = {0};  // position of last occurrence
      int r[NR] = {0};    // count repetition gaps of length r
      const unsigned char* p = in->data();
      if (level > 0) {
        for (unsigned i = 0; i < n; ++i) {
          const int k = i - pt[p[i]];
          if (k > 0 && k < NR) ++r[k];
          pt[p[i]] = i;
        }
      }

      // Add periodic models
      int n1 = n - r[1] - r[2] - r[3];
      for (int i = 0; i < 2; ++i) {
        int period = 0;
        double score = 0;
        int t = 0;
        for (int j = 5; j < NR && t < n1; ++j) {
          const double s = r[j] / (256.0 + n1 - t);
          if (s > score) score = s, period = j;
          t += r[j];
        }
        if (period > 4 && score > 0.1) {
          method += "c0,0," + itos(999 + period) + ",255i1";
          if (period <= 255) method += "c0," + itos(period) + "i1";
          n1 -= r[period];
          r[period] = 0;
        } else
          break;
      }
      method += "c0,2,0,255i1c0,3,0,0,255i1c0,4,0,0,0,255i1mm16ts19t0";
    }
  }

  // Compress
  std::string config;
  int args[9] = {0};
  config = make_config(method.c_str(), args);
  assert(n <= (0x100000u << args[0]) - 4096);
  BlockEncoder co;
  co.set_output(out);
  co.set_verify(true);
  io::Buffer pcomp_cmd;
  co.write_tag();
  co.start_block_config(config.c_str(), args, &pcomp_cmd);
  std::string cs = itos(n);
  if (comment) cs = cs + " " + comment;
  co.start_segment(filename ? filename : "", cs.c_str());
  if (args[1] >= 1 && args[1] <= 7 && args[1] != 4) {  // LZ77 or BWT
    LZBuffer lz(in->data(), n, args);
    co.set_input(&lz);
    co.compress(-1);
  } else {  // compress with e8e9 or no preprocessing
    if (args[1] >= 4 && args[1] <= 7) e8e9(in->data(), n);
    co.set_input(in);
    co.compress(-1);
  }
  const u8* sha1result = co.end_segment_checksum(dosha1);
  assert(sha1result);
  (void)sha1result;  // suppress unused warning with NDEBUG
  co.end_block();
  in->reset();  // drain the input, like libzpaq Buffer::read does
}

} // namespace zpaq_ng::compression
