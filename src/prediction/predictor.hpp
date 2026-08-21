// predictor.hpp - Context-mixing bit predictor.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The predictor consumes the COMP section of a block header and produces a
// 12 bit probability (0..4095) for the next bit using the ZPAQ component
// types: CONST, CM, ICM, MATCH, AVG, MIX2, MIX, ISSE and SSE. It is a
// faithful, modernized port of libzpaq's Predictor. The numeric behavior
// (table growth, weight adaptation, hash table replacement) is part of the
// format and is preserved exactly.

#ifndef ZPAQ_NG_PREDICTION_PREDICTOR_HPP
#define ZPAQ_NG_PREDICTION_PREDICTOR_HPP

#include "compression/zpaql_vm.hpp"
#include "core/types.hpp"
#include "memory/aligned_array.hpp"
#include "prediction/state_table.hpp"

namespace zpaq_ng::prediction {

// One ZPAQ component (model primitive).
struct Component {
  std::size_t limit = 0;  // max count for CM
  std::size_t cxt = 0;    // saved context
  std::size_t a = 0, b = 0, c = 0;  // multi-purpose
  memory::aligned_array<u32> cm;    // CM table / MATCH index / MIX weights
  memory::aligned_array<u8> ht;     // ICM/ISSE hash table rows, MATCH buffer
  memory::aligned_array<u16> a16;   // MIX2 weights
};

class Predictor {
public:
  explicit Predictor(compression::ZPAQL& z);
  Predictor(const Predictor&) = delete;
  Predictor& operator=(const Predictor&) = delete;

  // Build the model from the COMP section of z.header.
  void init();

  // Probability of the next bit as squash() of the model output (0..32767).
  int predict();

  // Train the model on bit y (0..1).
  void update(int y);

  // True when the block uses a context model (n > 0 components).
  bool is_modeled() const noexcept { return z_.header_size() > 6 && z_.num_components() != 0; }

  // Current partial byte (for diagnostics).
  int c8_state() const noexcept { return c8_; }

  // Per-component prediction (0..32767), for diagnostics.
  int stat(int x) { return p_[x]; }

private:
  int predict0();
  void update0(int y);

  // Locate/create the row for cxt in the hash table ht (rows of 16).
  std::size_t find(memory::aligned_array<u8>& ht, int sizebits, u32 cxt);

  // x -> floor(32768/(1+exp(-x/64))), x in -2048..2047.
  int squash(int x) const noexcept { return squasht_[x + 2048]; }

  // x -> round(64*log((x+0.5)/(32767.5-x))), x in 0..32767. Inverse of
  // squash, implemented as a lookup table indexed directly by x.
  int stretch(int x) const noexcept { return stretcht_[x]; }

  // Bound x to a 12 bit signed range.
  static int clamp2k(int x) noexcept {
    if (x < -2048) return -2048;
    if (x > 2047) return 2047;
    return x;
  }

  // Bound x to a 20 bit signed range.
  static int clamp512k(int x) noexcept {
    if (x < -(1 << 19)) return -(1 << 19);
    if (x >= (1 << 19)) return (1 << 19) - 1;
    return x;
  }

  // Reduce the prediction error in cr.cm. The packed word holds a 22 bit
  // prediction (bits 31..10) and a 10 bit count (bits 9..0).
  void train(Component& cr, int y) {
    u32& pn = cr.cm(cr.cxt);
    const u32 count = pn & 0x3ff;
    const int error = y * 32767 - static_cast<int>(pn >> 17);
    pn += static_cast<u32>((error * dt_[count] & -1024) + (count < cr.limit));
  }

  compression::ZPAQL& z_;
  Component comp_[256];
  u32 h_[256];       // cached H contexts
  StateTable st_;

  int c8_ = 1;       // last 0..7 bits, with leading 1
  int hmap4_ = 1;    // c8 split into nibbles
  int p_[256];       // per-component predictions
  bool tables_ready_ = false;

  int dt2k_[256];        // 2^12/i for match model
  int dt_[1024];         // 2^16/(i+1.5) for CM
  u16 squasht_[4096];    // squash lookup
  short stretcht_[32768];  // stretch lookup
};

} // namespace zpaq_ng::prediction

#endif // ZPAQ_NG_PREDICTION_PREDICTOR_HPP