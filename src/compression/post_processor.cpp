// post_processor.cpp - PCOMP loader and runner.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/post_processor.hpp"

namespace zpaq_ng::compression {

void PostProcessor::init(int h, int m) {
  state_ = hsize_ = 0;
  ph_ = h;
  pm_ = m;
  z.clear();
}

int PostProcessor::write(int c) {
  switch (state_) {
    case 0:  // initial state
      if (c < 0) throw format_error("unexpected EOS");
      state_ = c + 1;  // 1=PASS, 2=PROG
      if (state_ > 2) throw format_error("unknown post processing type");
      if (state_ == 1) z.clear();
      break;
    case 1:  // PASS
      z.outc(c);
      break;
    case 2:  // PROG
      if (c < 0) throw format_error("unexpected EOS");
      hsize_ = c;  // low byte of size
      state_ = 3;
      break;
    case 3:  // PROG psize[0]
      if (c < 0) throw format_error("unexpected EOS");
      hsize_ += c * 256;  // high byte of psize
      if (hsize_ < 1) throw format_error("empty PCOMP");
      z.pcomp_begin(hsize_, ph_, pm_);
      state_ = 4;
      break;
    case 4:  // PROG psize[0..1] pcomp[0...]
      if (c < 0) throw format_error("unexpected EOS");
      if (z.h_end() >= static_cast<int>(z.header_size()))
        throw format_error("PCOMP overflows header");
      z.pcomp_put(c);  // one byte of pcomp
      if (z.h_end() - z.h_begin() == hsize_) {  // last byte of pcomp?
        z.pcomp_finish();
        state_ = 5;
      }
      break;
    case 5:  // PROG ... data
      z.run(c);  // c is 0..255 or 0xFFFFFFFF
      if (c < 0) z.flush();
      break;
  }
  return state_;
}

} // namespace zpaq_ng::compression