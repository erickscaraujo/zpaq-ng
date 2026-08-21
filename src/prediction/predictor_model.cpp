// predictor_model.cpp - The interpreted COMP model: prediction and update.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Exact ports of the reference predictor loop. The arithmetic and table
// layout (packed words, hash row priority, match model bit packing) are
// part of the ZPAQ level 2 format and must match the reference bit for bit
// so that streams are interchangeable.

#include "prediction/predictor.hpp"

#include "core/error.hpp"

namespace zpaq_ng::prediction {

using zpaq_ng::compression::comp_size_bytes;

int Predictor::predict0() {
  const int n = z_.num_components();
  if (n == 0) return 0;

  const u8* cp = z_.header() + 7;
  for (int i = 0; i < n; ++i) {
    Component& cr = comp_[i];
    switch (cp[0]) {
      case 1:  // CONS: c
        break;
      case 2: {  // CM: sizebits limit
        cr.cxt = h_[i] ^ hmap4_;
        p_[i] = stretch(cr.cm(cr.cxt) >> 17);
        break;
      }
      case 3: {  // ICM: sizebits
        if (c8_ == 1 || (c8_ & 0xf0) == 16)
          cr.c = find(cr.ht, cp[1] + 2, h_[i] + 16 * c8_);
        cr.cxt = cr.ht[cr.c + (hmap4_ & 15)];
        p_[i] = stretch(cr.cm(cr.cxt) >> 8);
        break;
      }
      case 4: {  // MATCH: sizebits bufbits
                 // a=len, b=offset, c=bit, cxt=bitpos, ht=buf, limit=pos
        if (cr.a == 0)
          p_[i] = 0;
        else {
          cr.c = (cr.ht(cr.limit - cr.b) >> (7 - cr.cxt)) & 1;  // predicted bit
          p_[i] = stretch(dt2k_[cr.a] * (cr.c * -2 + 1) & 32767);
        }
        break;
      }
      case 5:  // AVG: j k wt
        p_[i] = (p_[cp[1]] * cp[3] + p_[cp[2]] * (256 - cp[3])) >> 8;
        break;
      case 6: {  // MIX2: sizebits j k rate mask
                 // c=size, a16=wt[size], cxt=input
        cr.cxt = ((h_[i] + (c8_ & cp[5])) & (cr.c - 1));
        const int w = cr.a16[cr.cxt];
        p_[i] = (w * p_[cp[2]] + (65536 - w) * p_[cp[3]]) >> 16;
        break;
      }
      case 7: {  // MIX: sizebits j m rate mask
                 // c=size, cm=wt[size][m], cxt=index of wt in cm
        const int m = cp[3];
        cr.cxt = h_[i] + (c8_ & cp[5]);
        cr.cxt = (cr.cxt & (cr.c - 1)) * m;  // pointer to row of weights
        int* wt = reinterpret_cast<int*>(&cr.cm[cr.cxt]);
        p_[i] = 0;
        for (int j = 0; j < m; ++j) p_[i] += (wt[j] >> 8) * p_[cp[2] + j];
        p_[i] = clamp2k(p_[i] >> 8);
        break;
      }
      case 8: {  // ISSE: sizebits j -- c=hi, cxt=bh
        if (c8_ == 1 || (c8_ & 0xf0) == 16)
          cr.c = find(cr.ht, cp[1] + 2, h_[i] + 16 * c8_);
        cr.cxt = cr.ht[cr.c + (hmap4_ & 15)];  // bit history
        int* wt = reinterpret_cast<int*>(&cr.cm[cr.cxt * 2]);
        p_[i] = clamp2k((wt[0] * p_[cp[2]] + wt[1] * 64) >> 16);
        break;
      }
      case 9: {  // SSE: sizebits j start limit
        cr.cxt = (h_[i] + c8_) * 32;
        int pq = p_[cp[2]] + 992;
        if (pq < 0) pq = 0;
        if (pq > 1983) pq = 1983;
        const int wt = pq & 63;
        pq >>= 6;
        cr.cxt += static_cast<std::size_t>(pq);
        p_[i] = stretch(
            ((cr.cm(cr.cxt) >> 10) * (64 - wt) + (cr.cm(cr.cxt + 1) >> 10) * wt) >> 13);
        cr.cxt += static_cast<std::size_t>(wt) >> 5;
        break;
      }
      default:
        throw format_error("component predict not implemented");
    }
    cp += comp_size_bytes[cp[0]];
  }
  return squash(p_[n - 1]);
}

void Predictor::update0(int y) {
  const int n = z_.num_components();
  if (n == 0) return;

  const u8* cp = z_.header() + 7;
  for (int i = 0; i < n; ++i) {
    Component& cr = comp_[i];
    switch (cp[0]) {
      case 1:  // CONS: c
        break;
      case 2:  // CM: sizebits limit
        train(cr, y);
        break;
      case 3: {  // ICM: sizebits
                 // cxt=ht[b]=bh, ht[c][0..15]=bh row
        cr.ht[cr.c + (hmap4_ & 15)] =
            static_cast<u8>(st_.next(cr.ht[cr.c + (hmap4_ & 15)], y));
        u32& pn = cr.cm(cr.cxt);
        pn += static_cast<u32>(int(y * 32767 - (pn >> 8)) >> 2);
        break;
      }
      case 4: {  // MATCH: sizebits bufbits
                 // a=len, b=offset, c=bit, cm=index, cxt=bitpos,
                 // ht=buf, limit=pos
        if (static_cast<int>(cr.c) != y) cr.a = 0;  // mismatch?
        cr.ht(cr.limit) += cr.ht(cr.limit) + y;     // shift in bit
        if (++cr.cxt == 8) {
          cr.cxt = 0;
          ++cr.limit;
          cr.limit &= (1u << cp[2]) - 1;
          if (cr.a == 0) {  // look for a match
            cr.b = cr.limit - cr.cm(h_[i]);
            if (cr.b & (cr.ht.size() - 1))
              while (cr.a < 255 &&
                     cr.ht(cr.limit - cr.a - 1) == cr.ht(cr.limit - cr.a - cr.b - 1))
                ++cr.a;
          } else {
            cr.a += cr.a < 255;
          }
          cr.cm(h_[i]) = static_cast<u32>(cr.limit);
        }
        break;
      }
      case 5:  // AVG: j k wt
        break;
      case 6: {  // MIX2: sizebits j k rate mask
                 // a16=wt[size], cxt=input
        const int err = (y * 32767 - squash(p_[i])) * cp[4] >> 5;
        int w = cr.a16[cr.cxt];
        w += (err * (p_[cp[2]] - p_[cp[3]]) + (1 << 12)) >> 13;
        if (w < 0) w = 0;
        if (w > 65535) w = 65535;
        cr.a16[cr.cxt] = static_cast<u16>(w);
        break;
      }
      case 7: {  // MIX: sizebits j m rate mask
                 // cm=wt[size][m], cxt=input
        const int m = cp[3];
        const int err = (y * 32767 - squash(p_[i])) * cp[4] >> 4;
        int* wt = reinterpret_cast<int*>(&cr.cm[cr.cxt]);
        for (int j = 0; j < m; ++j)
          wt[j] = clamp512k(wt[j] + ((err * p_[cp[2] + j] + (1 << 12)) >> 13));
        break;
      }
      case 8: {  // ISSE: sizebits j -- c=hi, cxt=bh
        const int err = y * 32767 - squash(p_[i]);
        int* wt = reinterpret_cast<int*>(&cr.cm[cr.cxt * 2]);
        wt[0] = clamp512k(wt[0] + ((err * p_[cp[2]] + (1 << 12)) >> 13));
        wt[1] = clamp512k(wt[1] + ((err + 16) >> 5));
        cr.ht[cr.c + (hmap4_ & 15)] = static_cast<u8>(st_.next(cr.cxt, y));
        break;
      }
      case 9:  // SSE: sizebits j start limit
        train(cr, y);
        break;
      default:
        throw format_error("component update not implemented");
    }
    cp += comp_size_bytes[cp[0]];
  }
}

} // namespace zpaq_ng::prediction