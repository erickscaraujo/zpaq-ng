// post_processor.hpp - PCOMP loader and runner (PostProcessor).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Some blocks store a postprocessor (PCOMP) in the first segment: a ZPAQL
// program that transforms the decompressed bytes back to the original file
// (for example an inverse LZ77 or E8E9 decoder). This module consumes the
// leading bytes of a segment, recognizes either PASS (no postprocessor) or
// PROG (a PCOMP program), loads the program into a ZPAQL machine, and then
// streams every subsequent byte through it while routing its OUT output to
// the destination Writer and/or SHA1.
//
// A faithful port of libzpaq's PostProcessor. The byte layout of the
// leading segment data is part of the format.

#ifndef ZPAQ_NG_COMPRESSION_POST_PROCESSOR_HPP
#define ZPAQ_NG_COMPRESSION_POST_PROCESSOR_HPP

#include "compression/zpaql_vm.hpp"
#include "integrity/sha1.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::compression {

class PostProcessor {
public:
  PostProcessor() = default;

  // Reset for a new segment. h and m are the ph, pm sizes copied from the
  // block header.
  void init(int h, int m);

  // Feed one byte (0..255) or -1 at end of segment. Returns the parse state
  // (1 = PASS, 5 = running postprocessor); get_state() & 3 == 1 when the
  // postprocessor is ready for data.
  int write(int c);

  // Parse state: 1 = PASS, 2..4 = loading PCOMP, 5 = POST.
  int get_state() const noexcept { return state_; }

  // Route OUT output.
  void set_output(io::Writer* out) { z.output = out; }
  void set_sha1(integrity::SHA1* sha1ptr) { z.sha1 = sha1ptr; }

  ZPAQL z;  // holds PCOMP

private:
  int state_ = 0;  // 0=INIT, 1=PASS, 2..4=loading, 5=POST
  int hsize_ = 0;  // PCOMP size
  int ph_ = 0;     // H size from block header
  int pm_ = 0;     // M size from block header
};

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_POST_PROCESSOR_HPP