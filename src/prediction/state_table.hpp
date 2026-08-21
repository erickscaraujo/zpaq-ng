// state_table.hpp - Bit history state table.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Maps an 8 bit bit-history state to the next state given the next bit and
// to an initial probability. The transition table (sns) is part of the ZPAQ
// format numerics and is reproduced verbatim in tables.inc.

#ifndef ZPAQ_NG_PREDICTION_STATE_TABLE_HPP
#define ZPAQ_NG_PREDICTION_STATE_TABLE_HPP

#include "core/types.hpp"

namespace zpaq_ng::prediction {

class StateTable {
public:
  StateTable();

  // Next state for bit y (0..1).
  int next(int state, int y) const noexcept {
    return ns_[state * 4 + y];
  }

  // Initial probability (as p*2^23) of a 1 given the counts in the state.
  int cminit(int state) const noexcept {
    return ((ns_[state * 4 + 3] * 2 + 1) << 22) /
           (ns_[state * 4 + 2] + ns_[state * 4 + 3] + 1);
  }

private:
  u8 ns_[1024];  // state*4 -> next if 0, next if 1, n0, n1
};

} // namespace zpaq_ng::prediction

#endif // ZPAQ_NG_PREDICTION_STATE_TABLE_HPP