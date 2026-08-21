// zpaql_vm.cpp - The ZPAQL virtual machine: header handling and execution
// driver. The instruction dispatch switch lives in zpaql_exec.cpp.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/zpaql_vm.hpp"

#include <cstring>

namespace zpaq_ng::compression {

const char* const comp_names[256] = {
    "", "const", "cm", "icm", "match", "avg", "mix2", "mix", "isse", "sse", 0};

namespace {
double pow2(int x) {
  double r = 1;
  for (; x > 0; --x) r += r;
  return r;
}
} // namespace

void ZPAQL::clear() {
  cend_ = hbegin_ = hend_ = 0;
  a_ = b_ = c_ = d_ = f_ = pc_ = 0;
  header_.resize(0);
  h_.resize(0);
  m_.resize(0);
  r_.resize(0);
  outbuf_.resize(1 << 14);
  bufptr_ = 0;
}

void ZPAQL::init(int hbits, int mbits) {
  if (hbits > 32) throw format_error("H too big");
  if (mbits > 32) throw format_error("M too big");
  h_.resize(1, hbits);
  m_.resize(1, mbits);
  r_.resize(256);
  a_ = b_ = c_ = d_ = pc_ = f_ = 0;
}

void ZPAQL::init_h() {
  if (header_.size() <= 6) throw format_error("missing HCOMP header");
  init(header_[2], header_[3]);
}

void ZPAQL::init_p() {
  if (header_.size() <= 6) throw format_error("missing PCOMP header");
  init(header_[4], header_[5]);
}

void ZPAQL::flush() {
  if (output) output->write(reinterpret_cast<const char*>(outbuf_.data()), bufptr_);
  if (sha1) sha1->write(reinterpret_cast<const char*>(outbuf_.data()), bufptr_);
  bufptr_ = 0;
}

double ZPAQL::memory() const {
  if (header_.size() <= 6) return 0;
  double mem = pow2(header_[2] + 2) + pow2(header_[3]) +  // hh hm
               pow2(header_[4] + 2) + pow2(header_[5]) +  // ph pm
               static_cast<double>(header_.size());
  int cp = 7;
  for (int i = 0; i < header_[6]; ++i) {
    const double size = pow2(header_[cp + 1]);
    switch (header_[cp]) {
      case 2: mem += 4 * size; break;                          // CM
      case 3: mem += 64 * size + 1024; break;                  // ICM
      case 4: mem += 4 * size + pow2(header_[cp + 2]); break;  // MATCH
      case 6: mem += 2 * size; break;                          // MIX2
      case 7: mem += 4 * size * header_[cp + 3]; break;        // MIX
      case 8: mem += 64 * size + 2048; break;                  // ISSE
      case 9: mem += 128 * size; break;                        // SSE
    }
    cp += comp_size_bytes[header_[cp]];
  }
  return mem;
}

bool ZPAQL::write_header(io::Writer* out2, bool pp) {
  if (header_.size() <= 6) return false;
  if (!pp) {
    for (int i = 0; i < cend_; ++i) out2->put(header_[i]);
  } else {
    out2->put((hend_ - hbegin_) & 255);
    out2->put((hend_ - hbegin_) >> 8);
  }
  for (int i = hbegin_; i < hend_; ++i) out2->put(header_[i]);
  return true;
}

int ZPAQL::read_header(io::Reader* in2) {
  int hsize = in2->get();
  hsize += in2->get() * 256;
  if (hsize < 7 || hsize > 0xFFFF) throw format_error("bad header size");
  header_.resize(static_cast<size_t>(hsize) + 300);
  cend_ = hbegin_ = hend_ = 0;
  header_[cend_++] = hsize & 255;
  header_[cend_++] = hsize >> 8;
  while (cend_ < 7) {
    const int b = in2->get();
    if (b < 0) throw format_error("unexpected end of file");
    header_[cend_++] = static_cast<u8>(b);
  }

  const int n = header_[cend_ - 1];
  for (int i = 0; i < n; ++i) {
    const int type = in2->get();
    if (type < 0 || type > 255) throw format_error("unexpected end of file");
    header_[cend_++] = static_cast<u8>(type);
    const int size = comp_size_bytes[type];
    if (size < 1) throw format_error("invalid component type");
    if (cend_ + size > hsize) throw format_error("COMP overflows header");
    for (int j = 1; j < size; ++j) {
      const int b = in2->get();
      if (b < 0) throw format_error("unexpected end of file");
      header_[cend_++] = static_cast<u8>(b);
    }
  }
  {
    const int b = in2->get();
    if (b != 0) throw format_error("missing COMP END");
    header_[cend_++] = static_cast<u8>(b);
  }

  hbegin_ = hend_ = cend_ + 128;
  if (hend_ > hsize + 129) throw format_error("missing HCOMP");
  while (hend_ < hsize + 129) {
    const int op = in2->get();
    if (op == -1) throw format_error("unexpected end of file");
    header_[hend_++] = static_cast<u8>(op);
  }
  {
    const int b = in2->get();
    if (b != 0) throw format_error("missing HCOMP END");
    header_[hend_++] = static_cast<u8>(b);
  }
  if (hsize != header_[0] + 256 * header_[1]) throw format_error("bad hsize");
  if (hsize != cend_ - 2 + hend_ - hbegin_) throw format_error("bad hsize");
  return cend_ + hend_ - hbegin_;
}

void ZPAQL::run0(u32 input) {
  pc_ = hbegin_;
  a_ = input;
  while (execute()) {
  }
}

void ZPAQL::run(u32 input) { run0(input); }

void ZPAQL::pcomp_begin(int psize, int ph, int pm) {
  header_.resize(static_cast<size_t>(psize) + 300);
  cend_ = 8;
  hbegin_ = hend_ = cend_ + 128;
  header_[4] = static_cast<u8>(ph);
  header_[5] = static_cast<u8>(pm);
}

void ZPAQL::pcomp_put(int c) { header_[hend_++] = static_cast<u8>(c); }

void ZPAQL::pcomp_finish() {
  const int hsize = cend_ - 2 + hend_ - hbegin_;
  header_[0] = static_cast<u8>(hsize & 255);
  header_[1] = static_cast<u8>(hsize >> 8);
  init_p();
}

[[noreturn]] void ZPAQL::err() { throw format_error("ZPAQL execution error"); }

} // namespace zpaq_ng::compression