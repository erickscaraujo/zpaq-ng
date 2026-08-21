// zpaql_compiler.hpp - ZPAQL configuration compiler (COMP/HCOMP/PCOMP).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Compiles a ZPAQL configuration string such as "comp 3 3 0 0 2 ..." into
// the byte coded COMP+HCOMP section (and optionally PCOMP) stored in a block
// header, exactly as libzpaq's Compiler does. This is what turns a method
// name like "x36,220,1" into the bytecode that produces a bitstream the
// original zpaq can decode.
//
// Faithful port of libzpaq.cpp Compiler (lines 2452-2770).

#ifndef ZPAQ_NG_COMPRESSION_ZPAQL_COMPILER_HPP
#define ZPAQ_NG_COMPRESSION_ZPAQL_COMPILER_HPP

#include <vector>

#include "compression/zpaql_vm.hpp"
#include "io/streams.hpp"

namespace zpaq_ng::compression {

// Compile a config string into ZPAQL headers. Throws format_error on
// malformed input. args is an optional array of 9 substitution arguments
// used by $1..$9 in the config; out2 receives the PCOMP command line when
// a PCOMP section is present (the text up to the terminating ';').
class Compiler {
public:
  Compiler(const char* in, const int* args, ZPAQL& hz, ZPAQL& pz,
           io::Writer* out2);

private:
  // Symbolic token codes shared by the opcode table and compile_comp.
  enum : int {
    JT = 39,  JF = 47,  JMP = 63,  LJ = 255,
    POST = 256, PCOMP = 257, END = 258, IF = 259, IFNOT = 260,
    ELSE = 261, ENDIF = 262, DO = 263, WHILE = 264, UNTIL = 265,
    FOREVER = 266, IFL = 267, IFNOTL = 268, ELSEL = 269, SEMICOLON = 270,
  };

  const char* in_;   // source, advanced by next()
  const int* args_;  // $1..$9 substitution values, or nullptr
  ZPAQL& hz_;        // COMP + HCOMP output
  ZPAQL& pz_;        // PCOMP output
  io::Writer* out2_; // PCOMP command output (or nullptr)
  int line_ = 1;
  int state_ = 0;    // 0=space, -1=word, >0=comment nesting

  std::vector<int> if_stack_;
  std::vector<int> do_stack_;

  void next();                       // advance in_ to the next token
  bool match_token(const char* word) const;
  [[noreturn]] void syntax_error(const char* msg, const char* expected = 0);
  int rtoken(const char* const* list); // token by position in list
  void rtoken(const char* s);          // token must equal s
  int rtoken(int low, int high);       // numeric token in [low, high]
  int compile_comp(ZPAQL& z);          // compile HCOMP or PCOMP
};

} // namespace zpaq_ng::compression

#endif // ZPAQ_NG_COMPRESSION_ZPAQL_COMPILER_HPP