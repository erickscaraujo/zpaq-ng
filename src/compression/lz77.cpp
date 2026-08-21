// lz77.cpp - LZ77/BWT preprocessing implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/lz77.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "compression/divsufsort.hpp"
#include "core/error.hpp"

namespace zpaq_ng::compression {

std::string itos(int64_t x, int n) {
  assert(x >= 0);
  assert(n >= 0);
  std::string r;
  for (; x || n > 0; x /= 10, --n) r = std::string(1, char('0' + x % 10)) + r;
  return r;
}

int lg(unsigned x) {
  unsigned r = 0;
  if (x >= 65536) r = 16, x >>= 16;
  if (x >= 256) r += 8, x >>= 8;
  if (x >= 16) r += 4, x >>= 4;
  assert(x < 16);
  static const int t[16] = {0, 1, 2, 2, 3, 3, 3, 3,
                            4, 4, 4, 4, 4, 4, 4, 4};
  return t[x] + r;
}

int nbits(unsigned x) {
  int r;
  for (r = 0; x; x >>= 1) r += x & 1;
  return r;
}

void e8e9(u8* buf, int n) {
  for (int i = n - 5; i >= 0; --i) {
    if (((buf[i] & 254) == 0xe8) && ((buf[i + 4] + 1) & 254) == 0) {
      unsigned a = (buf[i + 1] | buf[i + 2] << 8 | buf[i + 3] << 16) + i;
      buf[i + 1] = a;
      buf[i + 2] = a >> 8;
      buf[i + 3] = a >> 16;
    }
  }
}

namespace {
inline unsigned u32max(unsigned a, unsigned b) { return a > b ? a : b; }
} // namespace

std::size_t LZBuffer::read(char* p, std::size_t n) {
  if (rpos_ == wpos_) fill();
  std::size_t nr = n;
  if (nr > wpos_ - rpos_) nr = wpos_ - rpos_;
  if (nr) std::memcpy(p, buf_ + rpos_, nr);
  rpos_ += nr;
  assert(rpos_ <= wpos_);
  if (rpos_ == wpos_) rpos_ = wpos_ = 0;
  return nr;
}

LZBuffer::LZBuffer(const u8* in, size_t n, const int args[9],
                   const unsigned* sap)
    : ht_((args[1] & 3) == 3 ? (n + 1) * (sap == nullptr)
          : args[5] - args[0] < 21 ? 1u << args[5]
          : (n * (sap == nullptr)) + (1u << 17 << args[0])),
      in_(reinterpret_cast<const unsigned char*>(in)),
      checkbits_(args[5] - args[0] < 21 ? 12 - args[0] : 17 + args[0]),
      level_(args[1] & 3),
      htsize_(ht_.size()),
      n_(static_cast<unsigned>(n)),
      min_match_(args[2]),
      min_match2_(args[3]),
      max_match_(BUFSIZE * 3),
      max_literal_(BUFSIZE / 4),
      lookahead_(args[6]),
      bucket_((1u << args[4]) - 1),
      shift1_(min_match_ > 0 ? (args[5] - 1) / min_match_ + 1 : 1),
      shift2_(min_match2_ > 0 ? (args[5] - 1) / min_match2_ + 1 : 0),
      min_match_both_(u32max(min_match_, min_match2_ + lookahead_) + 4),
      rb_(args[0] > 4 ? args[0] - 4 : 0) {
  assert(args[0] >= 0);
  assert(n <= (1u << 20 << args[0]));
  assert(args[1] >= 1 && args[1] <= 7 && args[1] != 4);
  assert(level_ >= 1 && level_ <= 3);
  if ((min_match_ < 4 && level_ == 1) || (min_match_ < 1 && level_ == 2))
    throw format_error("match length $3 too small");

  // E8E9 transform.
  if (args[1] > 4 && !sap) e8e9(const_cast<u8*>(in), static_cast<int>(n));

  // Build the suffix array if not supplied.
  if (args[5] - args[0] >= 21 || level_ == 3) {  // LZ77-SA or BWT
    if (sap)
      sa_ = sap;
    else {
      assert(ht_.size() >= n);
      assert(ht_.size() > 0);
      sa_ = ht_.data();
      if (n > 0) divsufsort(in_, reinterpret_cast<int*>(ht_.data()), n);
    }
    if (level_ < 3) {
      assert(ht_.size() >= (n * (sap == nullptr)) + (1u << 17 << args[0]));
      isa_ = ht_.data() + n * (sap == nullptr);
    }
  }
}

void LZBuffer::fill() {
  // BWT
  if (level_ == 3) {
    assert(in_ || n_ == 0);
    assert(sa_);
    for (; wpos_ < BUFSIZE && i_ < n_ + 5; ++i_) {
      if (i_ == 0)
        put(n_ > 0 ? in_[n_ - 1] : 255);
      else if (i_ > n_)
        put(idx_ & 255), idx_ >>= 8;
      else if (sa_[i_ - 1] == 0)
        idx_ = i_, put(255);
      else
        put(in_[sa_[i_ - 1] - 1]);
    }
    return;
  }

  // LZ77: scan the input
  unsigned lit = 0;  // number of output literals pending
  const unsigned mask = (1u << checkbits_) - 1;
  while (i_ < n_ && wpos_ * 2 < BUFSIZE) {
    unsigned blen = min_match_ - 1;  // best match length
    unsigned bp = 0;                 // pointer to best match
    unsigned blit = 0;               // literals before best match
    int bscore = 0;                  // best cost

    // Look up contexts in suffix array
    if (isa_) {
      if (sa_[isa_[i_ & mask]] != i_)  // rebuild ISA
        for (unsigned j = 0; j < n_; ++j)
          if ((sa_[j] & ~mask) == (i_ & ~mask)) isa_[sa_[j] & mask] = j;
      for (unsigned h = 0; h <= lookahead_; ++h) {
        const unsigned q = isa_[(h + i_) & mask];  // location of h+i in SA
        assert(q < n_);
        if (sa_[q] != h + i_) continue;
        for (int j = -1; j <= 1; j += 2) {  // search backward and forward
          for (unsigned k = 1; k <= bucket_; ++k) {
            unsigned p;  // match to be tested
            if (q + j * k < n_ && (p = sa_[q + j * k] - h) < i_) {
              assert(p < n_);
              unsigned l, l1;  // length of match, leading literals
              for (l = h; i_ + l < n_ && l < max_match_ && in_[p + l] == in_[i_ + l]; ++l);
              for (l1 = h; l1 > 0 && in_[p + l1 - 1] == in_[i_ + l1 - 1]; --l1);
              int score = int(l - l1) * 8 - lg(i_ - p) - 4 * (lit == 0 && l1 > 0) - 11;
              for (unsigned a = 0; a < h; ++a) score = score * 5 / 8;
              if (score > bscore) blen = l, bp = p, blit = l1, bscore = score;
              if (l < blen || l < min_match_ || l > 255) break;
            }
          }
        }
        if (bscore <= 0 || blen < min_match_) break;
      }
    }

    // Look up contexts in a hash table
    else if (level_ == 1 || min_match_ <= 64) {
      if (min_match2_ > 0) {
        for (unsigned k = 0; k <= bucket_; ++k) {
          unsigned p = ht_[h2_ ^ k];
          if (p && (p & mask) == (in_[i_ + 3] & mask)) {
            p >>= checkbits_;
            if (p < i_ && i_ + blen <= n_ && in_[p + blen - 1] == in_[i_ + blen - 1]) {
              unsigned l;  // match length from lookahead
              for (l = lookahead_; i_ + l < n_ && l < max_match_ && in_[p + l] == in_[i_ + l]; ++l);
              if (l >= min_match2_ + lookahead_) {
                int l1;  // length back from lookahead
                for (l1 = lookahead_; l1 > 0 && in_[p + l1 - 1] == in_[i_ + l1 - 1]; --l1);
                assert(l1 >= 0 && l1 <= int(lookahead_));
                const int score = int(l - l1) * 8 - lg(i_ - p) - 8 * (lit == 0 && l1 > 0) - 11;
                if (score > bscore) blen = l, bp = p, blit = l1, bscore = score;
              }
            }
          }
          if (blen >= 128) break;
        }
      }

      // Search the lower order context
      if (!min_match2_ || blen < min_match2_) {
        for (unsigned k = 0; k <= bucket_; ++k) {
          unsigned p = ht_[h1_ ^ k];
          if (p && i_ + 3 < n_ && (p & mask) == (in_[i_ + 3] & mask)) {
            p >>= checkbits_;
            if (p < i_ && i_ + blen <= n_ && in_[p + blen - 1] == in_[i_ + blen - 1]) {
              unsigned l;
              for (l = 0; i_ + l < n_ && l < max_match_ && in_[p + l] == in_[i_ + l]; ++l);
              const int score = l * 8 - lg(i_ - p) - 2 * (lit > 0) - 11;
              if (score > bscore) blen = l, bp = p, blit = 0, bscore = score;
            }
          }
          if (blen >= 128) break;
        }
      }
    }

    // If the match is long enough, output any pending literals first and
    // then the match.
    assert(i_ >= bp);
    const unsigned off = i_ - bp;  // offset
    if (off > 0 && bscore > 0 &&
        blen - blit >= min_match_ + (level_ == 2) * ((off >= (1u << 16)) + (off >= (1u << 24)))) {
      lit += blit;
      write_literal(i_ + blit, lit);
      write_match(blen - blit, off);
    } else {  // add to literal length
      blen = 1;
      ++lit;
    }

    // Update index, advance blen bytes
    if (isa_)
      i_ += blen;
    else {
      while (blen--) {
        if (i_ + min_match_both_ < n_) {
          const unsigned ih = ((i_ * 1234547) >> 19) & bucket_;
          const unsigned p = (i_ << checkbits_) | (in_[i_ + 3] & mask);
          assert(ih <= bucket_);
          if (min_match2_) {
            ht_[h2_ ^ ih] = p;
            h2_ = (((h2_ * 9) << shift2_) + (in_[i_ + min_match2_ + lookahead_] + 1) * 23456789u) & (htsize_ - 1);
          }
          ht_[h1_ ^ ih] = p;
          h1_ = (((h1_ * 5) << shift1_) + (in_[i_ + min_match_] + 1) * 123456791u) & (htsize_ - 1);
        }
        ++i_;
      }
    }

    // Write long literals to keep buf from filling up
    if (lit >= max_literal_) write_literal(i_, lit);
  }

  // Write pending literals at end of input
  assert(i_ <= n_);
  if (i_ == n_) {
    write_literal(n_, lit);
    flush();
  }
}

void LZBuffer::write_literal(unsigned i, unsigned& lit) {
  assert(i <= n_);
  assert(i >= lit);
  if (level_ == 1) {
    if (lit < 1) return;
    int ll = lg(lit);
    assert(ll >= 1 && ll <= 24);
    putb(0, 2);
    --ll;
    while (--ll >= 0) {
      putb(1, 1);
      putb((lit >> ll) & 1, 1);
    }
    putb(0, 1);
    while (lit) putb(in_[i - lit--], 8);
  } else {
    assert(level_ == 2);
    while (lit > 0) {
      unsigned lit1 = lit;
      if (lit1 > 64) lit1 = 64;
      put(lit1 - 1);
      for (unsigned j = i - lit; j < i - lit + lit1; ++j) put(in_[j]);
      lit -= lit1;
    }
  }
}

void LZBuffer::write_match(unsigned len, unsigned off) {
  // mm,mmm,n,ll,r,q[mmmmm-8] = match n*4+ll, offset ((q-1)<<rb)+r+1
  if (level_ == 1) {
    assert(len >= min_match_ && len <= max_match_);
    assert(off > 0);
    assert(len >= 4);
    assert(rb_ <= 8);
    int ll = lg(len) - 1;
    assert(ll >= 2);
    off += (1u << rb_) - 1;
    const int lo = lg(off) - 1 - rb_;
    assert(lo >= 0 && lo <= 23);
    putb((lo + 8) >> 3, 2);  // mm
    putb(lo & 7, 3);         // mmm
    while (--ll >= 2) {      // n
      putb(1, 1);
      putb((len >> ll) & 1, 1);
    }
    putb(0, 1);
    putb(len & 3, 2);   // ll
    putb(off, rb_);     // r
    putb(off >> rb_, lo);  // q
  } else {
    assert(level_ == 2);
    assert(min_match_ >= 1 && min_match_ <= 64);
    --off;
    while (len > 0) {  // Split long matches to len1=minMatch..minMatch+63
      const unsigned len1 = len > min_match_ * 2 + 63   ? min_match_ + 63
                            : len > min_match_ + 63     ? len - min_match_
                            : len;
      assert(len1 >= min_match_ && len1 < min_match_ + 64);
      if (off < (1u << 16)) {
        put(64 + len1 - min_match_);
        put(off >> 8);
        put(off);
      } else if (off < (1u << 24)) {
        put(128 + len1 - min_match_);
        put(off >> 16);
        put(off >> 8);
        put(off);
      } else {
        put(192 + len1 - min_match_);
        put(off >> 24);
        put(off >> 16);
        put(off >> 8);
        put(off);
      }
      len -= len1;
    }
  }
}

} // namespace zpaq_ng::compression