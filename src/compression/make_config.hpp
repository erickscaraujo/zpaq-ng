// make_config.hpp - ZPAQ config file generator.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Faithful port of libzpaq's makeConfig (libzpaq.cpp 6885-7535). Expands a
// method string such as "14,220,1", "x4,3ci1" or "s" into a complete ZPAQ
// config text (COMP+HCOMP+PCOMP) and fills args[0..8] used by the LZBuffer
// preprocessing and block sizing.

#ifndef ZPAQ_NG_COMPRESSION_MAKE_CONFIG_HPP
#define ZPAQ_NG_COMPRESSION_MAKE_CONFIG_HPP

#include <string>

namespace zpaq_ng::compression {

// Generate a config file from the method argument with syntax:
// {0|x|s|i}[N1[,N2]...][{ciamtswf<cfg>}[N1[,N2]]...]...
// Returns the config text and fills args[0..8] ($1..$9).
std::string make_config(const char* method, int args[9]);

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_MAKE_CONFIG_HPP