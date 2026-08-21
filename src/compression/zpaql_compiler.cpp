// zpaql_compiler.cpp - ZPAQL configuration compiler implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "compression/zpaql_compiler.hpp"

#include <cassert>
#include <cstdlib>

namespace zpaq_ng::compression {

namespace {

// Component names by COMP code (terminated by nullptr).
const char* const compname[256] = {
    "", "const", "cm", "icm", "match", "avg", "mix2", "mix", "isse", "sse", 0};

// Opcode names by instruction byte. Indices 0..255 are instruction bytes,
// 256..270 the compiler keywords (POST..SEMICOLON), 271 the terminator.
const char* const opcodelist[272] = {
    "error", "a++", "a--", "a!", "a=0", "", "", "a=r",
    "b<>a", "b++", "b--", "b!", "b=0", "", "", "b=r",
    "c<>a", "c++", "c--", "c!", "c=0", "", "", "c=r",
    "d<>a", "d++", "d--", "d!", "d=0", "", "", "d=r",
    "*b<>a", "*b++", "*b--", "*b!", "*b=0", "", "", "jt",
    "*c<>a", "*c++", "*c--", "*c!", "*c=0", "", "", "jf",
    "*d<>a", "*d++", "*d--", "*d!", "*d=0", "", "", "r=a",
    "halt", "out", "", "hash", "hashd", "", "", "jmp",
    "a=a", "a=b", "a=c", "a=d", "a=*b", "a=*c", "a=*d", "a=",
    "b=a", "b=b", "b=c", "b=d", "b=*b", "b=*c", "b=*d", "b=",
    "c=a", "c=b", "c=c", "c=d", "c=*b", "c=*c", "c=*d", "c=",
    "d=a", "d=b", "d=c", "d=d", "d=*b", "d=*c", "d=*d", "d=",
    "*b=a", "*b=b", "*b=c", "*b=d", "*b=*b", "*b=*c", "*b=*d", "*b=",
    "*c=a", "*c=b", "*c=c", "*c=d", "*c=*b", "*c=*c", "*c=*d", "*c=",
    "*d=a", "*d=b", "*d=c", "*d=d", "*d=*b", "*d=*c", "*d=*d", "*d=",
    "", "", "", "", "", "", "", "",
    "a+=a", "a+=b", "a+=c", "a+=d", "a+=*b", "a+=*c", "a+=*d", "a+=",
    "a-=a", "a-=b", "a-=c", "a-=d", "a-=*b", "a-=*c", "a-=*d", "a-=",
    "a*=a", "a*=b", "a*=c", "a*=d", "a*=*b", "a*=*c", "a*=*d", "a*=",
    "a/=a", "a/=b", "a/=c", "a/=d", "a/=*b", "a/=*c", "a/=*d", "a/=",
    "a%=a", "a%=b", "a%=c", "a%=d", "a%=*b", "a%=*c", "a%=*d", "a%=",
    "a&=a", "a&=b", "a&=c", "a&=d", "a&=*b", "a&=*c", "a&=*d", "a&=",
    "a&~a", "a&~b", "a&~c", "a&~d", "a&~*b", "a&~*c", "a&~*d", "a&~",
    "a|=a", "a|=b", "a|=c", "a|=d", "a|=*b", "a|=*c", "a|=*d", "a|=",
    "a^=a", "a^=b", "a^=c", "a^=d", "a^=*b", "a^=*c", "a^=*d", "a^=",
    "a<<=a", "a<<=b", "a<<=c", "a<<=d", "a<<=*b", "a<<=*c", "a<<=*d", "a<<=",
    "a>>=a", "a>>=b", "a>>=c", "a>>=d", "a>>=*b", "a>>=*c", "a>>=*d", "a>>=",
    "a==a", "a==b", "a==c", "a==d", "a==*b", "a==*c", "a==*d", "a==",
    "a<a", "a<b", "a<c", "a<d", "a<*b", "a<*c", "a<*d", "a<",
    "a>a", "a>b", "a>c", "a>d", "a>*b", "a>*c", "a>*d", "a>",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "lj",
    "post", "pcomp", "end", "if", "ifnot", "else", "endif", "do",
    "while", "until", "forever", "ifl", "ifnotl", "elsel", ";", 0};

// Convert A-Z to lower case (ASCII only, as in the reference).
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c; }

} // namespace

void Compiler::next() {
  for (; *in_; ++in_) {
    if (*in_ == '\n') ++line_;
    if (*in_ == '(') state_ += 1 + (state_ < 0);
    else if (state_ > 0 && *in_ == ')') --state_;
    else if (state_ < 0 && *in_ <= ' ') state_ = 0;
    else if (state_ == 0 && *in_ > ' ') {
      state_ = -1;
      break;
    }
  }
  if (!*in_) throw format_error("unexpected end of config");
}

bool Compiler::match_token(const char* word) const {
  const char* a = in_;
  for (; (*a > ' ' && *a != '(' && *word); ++a, ++word)
    if (tolower(*a) != tolower(*word)) return false;
  return !*word && (*a <= ' ' || *a == '(');
}

void Compiler::syntax_error(const char* msg, const char* expected) {
  std::string s = "Config line ";
  s += std::to_string(line_);
  s += " at ";
  for (int i = 0; i < 40 && *in_ > ' '; ++i) s += *in_++;
  s += ": ";
  s += msg;
  if (expected) {
    s += ", expected: ";
    s += expected;
  }
  throw format_error(s);
}

int Compiler::rtoken(const char* const* list) {
  next();
  for (int i = 0; list[i]; ++i)
    if (match_token(list[i])) return i;
  syntax_error("unexpected");
}

void Compiler::rtoken(const char* s) {
  next();
  if (!match_token(s)) syntax_error("expected", s);
}

int Compiler::rtoken(int low, int high) {
  next();
  int r = 0;
  if (in_[0] == '$' && in_[1] >= '1' && in_[1] <= '9') {
    if (in_[2] == '+') r = std::atoi(in_ + 3);
    if (args_) r += args_[in_[1] - '1'];
  } else if (in_[0] == '-' || (in_[0] >= '0' && in_[0] <= '9'))
    r = std::atoi(in_);
  else
    syntax_error("expected a number");
  if (r < low) syntax_error("number too low");
  if (r > high) syntax_error("number too high");
  return r;
}

int Compiler::compile_comp(ZPAQL& z) {
  int op = 0;
  const int comp_begin = z.h_end();
  while (true) {
    op = rtoken(opcodelist);
    if (op == POST || op == PCOMP || op == END) break;
    int operand = -1;   // 0...255 if 2 bytes
    int operand2 = -1;  // 0...255 if 3 bytes
    if (op == IF) {
      op = JF;
      operand = 0;  // set later
      if_stack_.push_back(z.h_end() + 1);  // save jump target location
    } else if (op == IFNOT) {
      op = JT;
      operand = 0;
      if_stack_.push_back(z.h_end() + 1);
    } else if (op == IFL || op == IFNOTL) {  // long if
      z.header(z.h_end()++) = (op == IFL) ? JT : JF;
      z.header(z.h_end()++) = 3;
      op = LJ;
      operand = operand2 = 0;
      if_stack_.push_back(z.h_end() + 1);
    } else if (op == ELSE || op == ELSEL) {
      if (op == ELSE) op = JMP, operand = 0;
      if (op == ELSEL) op = LJ, operand = operand2 = 0;
      const int a = if_stack_.back();
      if_stack_.pop_back();
      assert(a > comp_begin && a < static_cast<int>(z.h_end()));
      if (z.header(a - 1) != LJ) {  // IF, IFNOT
        assert(z.header(a - 1) == JT || z.header(a - 1) == JF ||
               z.header(a - 1) == JMP);
        const int j = z.h_end() - a + 1 + (op == LJ);  // offset at IF
        assert(j >= 0);
        if (j > 127) syntax_error("IF too big, try IFL, IFNOTL");
        z.header(a) = j;
      } else {  // IFL, IFNOTL
        const int j = z.h_end() - comp_begin + 2 + (op == LJ);
        assert(j >= 0);
        z.header(a) = j & 255;
        z.header(a + 1) = (j >> 8) & 255;
      }
      if_stack_.push_back(z.h_end() + 1);  // save JMP target location
    } else if (op == ENDIF) {
      const int a = if_stack_.back();
      if_stack_.pop_back();
      assert(a > comp_begin && a < static_cast<int>(z.h_end()));
      int j = z.h_end() - a - 1;  // jump offset
      assert(j >= 0);
      if (z.header(a - 1) != LJ) {
        assert(z.header(a - 1) == JT || z.header(a - 1) == JF ||
               z.header(a - 1) == JMP);
        if (j > 127) syntax_error("IF too big, try IFL, IFNOTL, ELSEL");
        z.header(a) = j;
      } else {
        assert(a + 1 < static_cast<int>(z.h_end()));
        j = z.h_end() - comp_begin;
        z.header(a) = j & 255;
        z.header(a + 1) = (j >> 8) & 255;
      }
    } else if (op == DO) {
      do_stack_.push_back(z.h_end());
    } else if (op == WHILE || op == UNTIL || op == FOREVER) {
      const int a = do_stack_.back();
      do_stack_.pop_back();
      assert(a >= comp_begin && a < static_cast<int>(z.h_end()));
      int j = a - z.h_end() - 2;
      assert(j <= -2);
      if (j >= -127) {  // backward short jump
        if (op == WHILE) op = JT;
        if (op == UNTIL) op = JF;
        if (op == FOREVER) op = JMP;
        operand = j & 255;
      } else {  // backward long jump
        j = a - comp_begin;
        assert(j >= 0 && j < static_cast<int>(z.h_end()) - comp_begin);
        if (op == WHILE) {
          z.header(z.h_end()++) = JF;
          z.header(z.h_end()++) = 3;
        }
        if (op == UNTIL) {
          z.header(z.h_end()++) = JT;
          z.header(z.h_end()++) = 3;
        }
        op = LJ;
        operand = j & 255;
        operand2 = j >> 8;
      }
    } else if ((op & 7) == 7) {  // 2 byte operand, read N
      if (op == LJ) {
        operand = rtoken(0, 65535);
        operand2 = operand >> 8;
        operand &= 255;
      } else if (op == JT || op == JF || op == JMP) {
        operand = rtoken(-128, 127);
        operand &= 255;
      } else
        operand = rtoken(0, 255);
    }
    if (op >= 0 && op <= 255) z.header(z.h_end()++) = static_cast<u8>(op);
    if (operand >= 0) z.header(z.h_end()++) = static_cast<u8>(operand);
    if (operand2 >= 0) z.header(z.h_end()++) = static_cast<u8>(operand2);
    if (z.h_end() >= static_cast<int>(z.header_size()) - 130 ||
        z.h_end() - z.h_begin() + z.comp_end() - 2 > 65535)
      syntax_error("program too big");
  }
  z.header(z.h_end()++) = 0;  // END
  return op;
}

Compiler::Compiler(const char* in, const int* args, ZPAQL& hz, ZPAQL& pz,
                   io::Writer* out2)
    : in_(in), args_(args), hz_(hz), pz_(pz), out2_(out2), line_(1), state_(0) {
  hz_.clear();
  pz_.clear();
  hz_.resize_header(68000);

  // Compile the COMP section of header.
  rtoken("comp");
  hz_.header(2) = static_cast<u8>(rtoken(0, 255));  // hh
  hz_.header(3) = static_cast<u8>(rtoken(0, 255));  // hm
  hz_.header(4) = static_cast<u8>(rtoken(0, 255));  // ph
  hz_.header(5) = static_cast<u8>(rtoken(0, 255));  // pm
  const int n = hz_.header(6) = static_cast<u8>(rtoken(0, 255));  // n
  hz_.comp_end() = 7;
  for (int i = 0; i < n; ++i) {
    rtoken(i, i);
    const int type = rtoken(compname);
    hz_.header(hz_.comp_end()++) = static_cast<u8>(type);
    const int clen = comp_size_bytes[type & 255];
    if (clen < 1 || clen > 10) syntax_error("invalid component");
    for (int j = 1; j < clen; ++j)
      hz_.header(hz_.comp_end()++) = static_cast<u8>(rtoken(0, 255));
  }
  hz_.header(hz_.comp_end()++);  // end
  hz_.h_begin() = hz_.h_end() = hz_.comp_end() + 128;

  // Compile HCOMP.
  rtoken("hcomp");
  int op = compile_comp(hz_);

  // Compute header size.
  const int hsize = hz_.comp_end() - 2 + hz_.h_end() - hz_.h_begin();
  hz_.header(0) = hsize & 255;
  hz_.header(1) = hsize >> 8;

  // Compile POST 0 END.
  if (op == POST) {
    rtoken(0, 0);
    rtoken("end");
  }

  // Compile PCOMP pcomp_cmd ; program... END.
  else if (op == PCOMP) {
    pz_.resize_header(68000);
    pz_.header(4) = hz_.header(4);  // ph
    pz_.header(5) = hz_.header(5);  // pm
    pz_.comp_end() = 8;
    pz_.h_begin() = pz_.h_end() = pz_.comp_end() + 128;

    // Get the pcomp_cmd ending with ";" (case sensitive).
    next();
    while (*in_ && *in_ != ';') {
      if (out2_) out2_->put(*in_);
      ++in_;
    }
    if (*in_) ++in_;

    // Compile PCOMP.
    op = compile_comp(pz_);
    const int len = pz_.comp_end() - 2 + pz_.h_end() - pz_.h_begin();
    assert(len >= 0);
    pz_.header(0) = len & 255;
    pz_.header(1) = len >> 8;
    if (op != END) syntax_error("expected END");
  } else if (op != END)
    syntax_error("expected END or POST 0 END or PCOMP cmd ; ... END");
}

} // namespace zpaq_ng::compression