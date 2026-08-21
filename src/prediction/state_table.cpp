// state_table.cpp - Bit history state table.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "prediction/state_table.hpp"

#include <cstring>

// Generated ZPAQ-compatible tables (sns, sdt2k, sdt, ssquasht, stdt).
#include "prediction/tables.inc"

namespace zpaq_ng::prediction {

StateTable::StateTable() { std::memcpy(ns_, sns, sizeof(ns_)); }

} // namespace zpaq_ng::prediction