// predictor.cpp - Model construction and probability tables.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The squash/stretch lookup tables are reconstructed at first use from the
// ZPAQ-compatible constants in tables.inc, exactly as the reference does.

#include "prediction/predictor.hpp"

#include <cstring>

#include "core/error.hpp"
#include "prediction/tables.inc"

namespace zpaq_ng::prediction {

using zpaq_ng::compression::comp_size_bytes;

Predictor::Predictor(compression::ZPAQL& z) : z_(z) {}

// Initialize the model independent tables on first use.
static void build_tables(int dt2k[256], int dt[1024], u16 squasht[4096],
                         short stretcht[32768]) {
  std::memcpy(dt2k, sdt2k, 256 * sizeof(int));
  std::memcpy(dt, sdt, 1024 * sizeof(int));

  // squasht[i] = int(32768.0/(1+exp((i-2048)*(-1.0/64))));
  // The generated table covers the middle 1344 of 4096 entries.
  std::memset(squasht, 0, 1376 * sizeof(u16));
  std::memcpy(squasht + 1376, ssquasht, 1344 * sizeof(u16));
  for (int i = 2720; i < 4096; ++i) squasht[i] = 32767;

  // stretcht[i] = int(log((i+0.5)/(32767.5-i))*64+0.5+100000)-100000;
  int k = 16384;
  for (int i = 0; i < 712; ++i)
    for (int j = stdt[i]; j > 0; --j) stretcht[k++] = static_cast<short>(i);
  for (int i = 0; i < 16384; ++i) stretcht[i] = -stretcht[32767 - i];
}

void Predictor::init() {
  if (!tables_ready_ && is_modeled()) {
    tables_ready_ = true;
    build_tables(dt2k_, dt_, squasht_, stretcht_);
  }

  // Initialize the machine that computes contexts.
  z_.init_h();

  for (int i = 0; i < 256; ++i) h_[i] = p_[i] = 0;
  for (int i = 0; i < 256; ++i) comp_[i] = Component();

  const int n = z_.num_components();
  const u8* cp = z_.header() + 7;
  for (int i = 0; i < n; ++i) {
    Component& cr = comp_[i];
    switch (cp[0]) {
      case 1:  // CONS: prediction (c-128)*4
        p_[i] = (cp[1] - 128) * 4;
        break;
      case 2: {  // CM sizebits limit
        if (cp[1] > 32) throw format_error("max size for CM is 32");
        cr.cm.resize(1, cp[1]);  // packed (22 bits) + count (10 bits)
        cr.limit = cp[2] * 4;
        for (std::size_t j = 0; j < cr.cm.size(); ++j) cr.cm[j] = 0x80000000u;
        break;
      }
      case 3: {  // ICM sizebits
        if (cp[1] > 26) throw format_error("max size for ICM is 26");
        cr.limit = 1023;
        cr.cm.resize(256);
        cr.ht.resize(64, cp[1]);
        for (std::size_t j = 0; j < cr.cm.size(); ++j) cr.cm[j] = st_.cminit(static_cast<int>(j));
        break;
      }
      case 4: {  // MATCH sizebits bufbits
        if (cp[1] > 32 || cp[2] > 32)
          throw format_error("max size for MATCH is 32 32");
        cr.cm.resize(1, cp[1]);  // index
        cr.ht.resize(1, cp[2]);  // history buffer
        cr.ht(0) = 1;
        break;
      }
      case 5:  // AVG j k wt
        if (cp[1] >= i) throw format_error("AVG j >= i");
        if (cp[2] >= i) throw format_error("AVG k >= i");
        break;
      case 6: {  // MIX2 sizebits j k rate mask
        if (cp[1] > 32) throw format_error("max size for MIX2 is 32");
        if (cp[2] >= i) throw format_error("MIX2 j >= i");
        if (cp[3] >= i) throw format_error("MIX2 k >= i");
        cr.c = std::size_t(1) << cp[1];
        cr.a16.resize(1, cp[1]);
        for (std::size_t j = 0; j < cr.a16.size(); ++j) cr.a16[j] = 32768;
        break;
      }
      case 7: {  // MIX sizebits j m rate mask
        if (cp[1] > 32) throw format_error("max size for MIX is 32");
        if (cp[2] >= i) throw format_error("MIX j >= i");
        if (cp[3] < 1 || cp[3] > i - cp[2])
          throw format_error("MIX m not in 1..i-j");
        const int m = cp[3];
        cr.c = std::size_t(1) << cp[1];
        cr.cm.resize(m, cp[1]);  // weights wt[size][m]
        for (std::size_t j = 0; j < cr.cm.size(); ++j) cr.cm[j] = 65536 / m;
        break;
      }
      case 8: {  // ISSE sizebits j
        if (cp[1] > 32) throw format_error("max size for ISSE is 32");
        if (cp[2] >= i) throw format_error("ISSE j >= i");
        cr.ht.resize(64, cp[1]);
        cr.cm.resize(512);
        for (int j = 0; j < 256; ++j) {
          cr.cm[j * 2] = 1 << 15;
          cr.cm[j * 2 + 1] = static_cast<u32>(clamp512k(stretch(st_.cminit(j) >> 8) * 1024));
        }
        break;
      }
      case 9: {  // SSE sizebits j start limit
        if (cp[1] > 32) throw format_error("max size for SSE is 32");
        if (cp[2] >= i) throw format_error("SSE j >= i");
        if (cp[3] > cp[4] * 4) throw format_error("SSE start > limit*4");
        cr.cm.resize(32, cp[1]);
        cr.limit = cp[4] * 4;
        for (std::size_t j = 0; j < cr.cm.size(); ++j)
          cr.cm[j] = (static_cast<u32>(squash((static_cast<int>(j) & 31) * 64 - 992))
                      << 17) |
                     cp[3];
        break;
      }
      default:
        throw format_error("unknown component type");
    }
    cp += comp_size_bytes[cp[0]];
  }
}

// Locate the row for cxt in ht (rows of 16, hashed). On miss after 3
// adjacent probes, replace the row with the lowest priority byte.
std::size_t Predictor::find(memory::aligned_array<u8>& ht, int sizebits,
                            u32 cxt) {
  const int chk = static_cast<int>(cxt >> sizebits) & 255;
  const std::size_t h0 = (static_cast<std::size_t>(cxt) * 16) & (ht.size() - 16);
  if (ht[h0] == chk) return h0;
  const std::size_t h1 = h0 ^ 16;
  if (ht[h1] == chk) return h1;
  const std::size_t h2 = h0 ^ 32;
  if (ht[h2] == chk) return h2;
  if (ht[h0 + 1] <= ht[h1 + 1] && ht[h0 + 1] <= ht[h2 + 1]) {
    std::memset(&ht[h0], 0, 16);
    ht[h0] = static_cast<u8>(chk);
    return h0;
  }
  if (ht[h1 + 1] < ht[h2 + 1]) {
    std::memset(&ht[h1], 0, 16);
    ht[h1] = static_cast<u8>(chk);
    return h1;
  }
  std::memset(&ht[h2], 0, 16);
  ht[h2] = static_cast<u8>(chk);
  return h2;
}

int Predictor::predict() { return predict0(); }

void Predictor::update(int y) {
  update0(y);

  // Save bit y in c8, hmap4.
  c8_ += c8_ + y;
  if (c8_ >= 256) {
    z_.run(c8_ - 256);
    hmap4_ = 1;
    c8_ = 1;
    for (int i = 0; i < z_.num_components(); ++i) h_[i] = z_.H(i);
  } else if (c8_ >= 16 && c8_ < 32) {
    hmap4_ = (hmap4_ & 0xf) << 5 | y << 4 | 1;
  } else {
    hmap4_ = (hmap4_ & 0x1f0) | (((hmap4_ & 0xf) * 2 + y) & 0xf);
  }
}

} // namespace zpaq_ng::prediction