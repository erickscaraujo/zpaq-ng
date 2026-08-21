// zpaql_vm.hpp - The ZPAQL virtual machine (HCOMP/PCOMP executor).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// ZPAQ archives are self-describing: the block header contains a byte-coded
// program (ZPAQL) that computes the context hashes (HCOMP) during both
// compression and decompression, and an optional postprocessor (PCOMP) that
// inverts any reversible transform applied before coding. Because the program
// is stored in the archive, a single interpreter can decode every block the
// original zpaq has ever produced.
//
// This module is a faithful, modernized port of libzpaq's ZPAQL class. The
// instruction set, the header layout, and the 32 bit wrapping arithmetic are
// part of the format and are preserved exactly.

#ifndef ZPAQ_NG_COMPRESSION_ZPAQL_VM_HPP
#define ZPAQ_NG_COMPRESSION_ZPAQL_VM_HPP

#include <cstddef>

#include "core/error.hpp"
#include "core/types.hpp"
#include "io/streams.hpp"
#include "integrity/sha1.hpp"
#include "memory/aligned_array.hpp"

namespace zpaq_ng::compression {

// Component type codes used in the COMP section of a block header.
enum class ComponentType : u8 {
  NONE = 0,
  CONS = 1,
  CM = 2,
  ICM = 3,
  MATCH = 4,
  AVG = 5,
  MIX2 = 6,
  MIX = 7,
  ISSE = 8,
  SSE = 9
};

// Number of header bytes used to encode each component type.
constexpr int comp_size_bytes[256] = {
    1, 2, 3, 2, 3, 4, 6, 6, 3, 5,
};

// Human readable names for the component types (used by diagnostics).
extern const char* const comp_names[256];

// A ZPAQL machine: one instance for HCOMP (contexts) and one for PCOMP
// (postprocessing). Both share the same instruction set; PCOMP additionally
// can emit bytes through OUT.
class ZPAQL {
public:
  ZPAQL() { clear(); }

  // Free memory and reset machine state.
  void clear();

  // Initialize the machine to run as HCOMP (uses hh, hm from the header).
  void init_h();

  // Initialize the machine to run as PCOMP (uses ph, pm from the header).
  void init_p();

  // Approximate memory required by this machine in bytes.
  double memory() const;

  // Execute the program with the given input byte (0..255, or 0xFFFFFFFF
  // as the end-of-segment sentinel for PCOMP).
  void run(u32 input);

  // Read a block header from in2. Returns the header size.
  int read_header(io::Reader* in2);

  // Write the COMP+HCOMP section (pp=false) or only the PCOMP section
  // header (pp=true) to out2.
  bool write_header(io::Writer* out2, bool pp);

  // ---- PCOMP header construction (used by the PostProcessor) ----
  // Begin building a PCOMP header in place. psize is the expected number of
  // program bytes; the header is laid out as n=0 components plus the program.
  void pcomp_begin(int psize, int ph, int pm);

  // Append one byte of PCOMP code.
  void pcomp_put(int c);

  // Finalize the PCOMP header (write hsize) and initialize as PCOMP.
  void pcomp_finish();

  // Read-only access to the header bytes.
  const u8* header() const noexcept { return header_.data(); }
  std::size_t header_size() const noexcept { return header_.size(); }
  int comp_end() const noexcept { return cend_; }
  int h_begin() const noexcept { return hbegin_; }
  int h_end() const noexcept { return hend_; }

  // Mutable access used while building a header (Compiler port).
  u8& header(int i) noexcept { return header_[static_cast<size_t>(i)]; }
  const u8& header(int i) const noexcept {
    return header_[static_cast<size_t>(i)];
  }
  void resize_header(std::size_t n) { header_.resize(n); }
  int& comp_end() noexcept { return cend_; }
  int& h_begin() noexcept { return hbegin_; }
  int& h_end() noexcept { return hend_; }

  // Number of components in the model.
  int num_components() const noexcept {
    return header_.size() > 6 ? header_[6] : 0;
  }

  // H[i] - context hash array element (used by the predictor).
  u32 H(int i) const { return h_(static_cast<size_t>(i)); }

  // ---- PCOMP output plumbing ----
  io::Writer* output = nullptr;
  integrity::SHA1* sha1 = nullptr;

  // Write buffered output bytes to output and/or sha1.
  void flush();

  // Output one byte (0..255) or -1 at end of stream.
  void outc(int ch) {
    if (ch < 0 || (outbuf_[bufptr_] = static_cast<u8>(ch),
                   ++bufptr_ == static_cast<int>(outbuf_.size())))
      flush();
  }

private:
  void init(int hbits, int mbits);
  int execute();  // interpret one instruction; 0 after HALT
  void run0(u32 input);
  void div(u32 x) {
    if (x) a_ /= x;
    else a_ = 0;
  }
  void mod(u32 x) {
    if (x) a_ %= x;
    else a_ = 0;
  }
  void swap(u32& x) {
    a_ ^= x;
    x ^= a_;
    a_ ^= x;
  }
  void swap(u8& x) {
    a_ ^= x;
    x ^= a_;
    a_ ^= x;
  }
  [[noreturn]] void err();

  // Header storage (layout identical to libzpaq):
  //   [0..1] hsize little endian
  //   [2..6] hh hm ph pm n
  //   [7..cend-1] component list; [cend-1] == 0 guard
  //   [cend..hbegin-1] 128 byte guard gap
  //   [hbegin..hend-1] HCOMP/PCOMP bytecode; [hend] == 0 guard
  memory::aligned_array<u8> header_;
  int cend_ = 0;
  int hbegin_ = 0;
  int hend_ = 0;

  memory::aligned_array<u8> m_;      // M array (byte memory)
  memory::aligned_array<u32> h_;     // H array (context hashes)
  memory::aligned_array<u32> r_;     // 256 registers
  memory::aligned_array<u8> outbuf_; // PCOMP output buffer
  int bufptr_ = 0;

  u32 a_ = 0, b_ = 0, c_ = 0, d_ = 0;  // machine registers
  int f_ = 0;                           // condition flag
  int pc_ = 0;                          // program counter
};

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_ZPAQL_VM_HPP