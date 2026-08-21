// zpaql_exec.cpp - The ZPAQL instruction dispatch switch.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Dispatches on the 256 opcode values exactly as the reference interpreter.
// All arithmetic wraps modulo 2^32 and memory access goes through the
// power-of-two masking operators of aligned_array. HALT returns 0.

#include "compression/zpaql_vm.hpp"

namespace zpaq_ng::compression {

int ZPAQL::execute() {
  switch (header_[pc_++]) {
    case 0: err(); break;                                // ERROR
    case 1: ++a_; break;                                 // A++
    case 2: --a_; break;                                 // A--
    case 3: a_ = ~a_; break;                             // A!
    case 4: a_ = 0; break;                               // A=0
    case 7: a_ = r_[header_[pc_++]]; break;              // A=R N
    case 8: swap(b_); break;                             // B<>A
    case 9: ++b_; break;                                 // B++
    case 10: --b_; break;                                // B--
    case 11: b_ = ~b_; break;                            // B!
    case 12: b_ = 0; break;                              // B=0
    case 15: b_ = r_[header_[pc_++]]; break;             // B=R N
    case 16: swap(c_); break;                            // C<>A
    case 17: ++c_; break;                                // C++
    case 18: --c_; break;                                // C--
    case 19: c_ = ~c_; break;                            // C!
    case 20: c_ = 0; break;                              // C=0
    case 23: c_ = r_[header_[pc_++]]; break;             // C=R N
    case 24: swap(d_); break;                            // D<>A
    case 25: ++d_; break;                                // D++
    case 26: --d_; break;                                // D--
    case 27: d_ = ~d_; break;                            // D!
    case 28: d_ = 0; break;                              // D=0
    case 31: d_ = r_[header_[pc_++]]; break;             // D=R N
    case 32: swap(m_(b_)); break;                        // *B<>A
    case 33: ++m_(b_); break;                            // *B++
    case 34: --m_(b_); break;                            // *B--
    case 35: m_(b_) = static_cast<u8>(~m_(b_)); break;   // *B!
    case 36: m_(b_) = 0; break;                          // *B=0
    case 39:  // JT N
      if (f_) pc_ += ((header_[pc_] + 128) & 255) - 127;
      else ++pc_;
      break;
    case 40: swap(m_(c_)); break;                        // *C<>A
    case 41: ++m_(c_); break;                            // *C++
    case 42: --m_(c_); break;                            // *C--
    case 43: m_(c_) = static_cast<u8>(~m_(c_)); break;   // *C!
    case 44: m_(c_) = 0; break;                          // *C=0
    case 47:  // JF N
      if (!f_) pc_ += ((header_[pc_] + 128) & 255) - 127;
      else ++pc_;
      break;
    case 48: swap(h_(d_)); break;                        // *D<>A
    case 49: ++h_(d_); break;                            // *D++
    case 50: --h_(d_); break;                            // *D--
    case 51: h_(d_) = ~h_(d_); break;                    // *D!
    case 52: h_(d_) = 0; break;                          // *D=0
    case 55: r_[header_[pc_++]] = a_; break;             // R=A N
    case 56: return 0;                                   // HALT
    case 57: outc(a_ & 255); break;                      // OUT
    case 59: a_ = (a_ + m_(b_) + 512) * 773; break;      // HASH
    case 60: h_(d_) = (h_(d_) + a_ + 512) * 773; break;  // HASHD
    case 63: pc_ += ((header_[pc_] + 128) & 255) - 127; break;  // JMP N
    case 64: break;                                      // A=A
    case 65: a_ = b_; break;                             // A=B
    case 66: a_ = c_; break;                             // A=C
    case 67: a_ = d_; break;                             // A=D
    case 68: a_ = m_(b_); break;                         // A=*B
    case 69: a_ = m_(c_); break;                         // A=*C
    case 70: a_ = h_(d_); break;                         // A=*D
    case 71: a_ = header_[pc_++]; break;                 // A= N
    case 72: b_ = a_; break;                             // B=A
    case 73: break;                                      // B=B
    case 74: b_ = c_; break;                             // B=C
    case 75: b_ = d_; break;                             // B=D
    case 76: b_ = m_(b_); break;                         // B=*B
    case 77: b_ = m_(c_); break;                         // B=*C
    case 78: b_ = h_(d_); break;                         // B=*D
    case 79: b_ = header_[pc_++]; break;                 // B= N
    case 80: c_ = a_; break;                             // C=A
    case 81: c_ = b_; break;                             // C=B
    case 82: break;                                      // C=C
    case 83: c_ = d_; break;                             // C=D
    case 84: c_ = m_(b_); break;                         // C=*B
    case 85: c_ = m_(c_); break;                         // C=*C
    case 86: c_ = h_(d_); break;                         // C=*D
    case 87: c_ = header_[pc_++]; break;                 // C= N
    case 88: d_ = a_; break;                             // D=A
    case 89: d_ = b_; break;                             // D=B
    case 90: d_ = c_; break;                             // D=C
    case 91: break;                                      // D=D
    case 92: d_ = m_(b_); break;                         // D=*B
    case 93: d_ = m_(c_); break;                         // D=*C
    case 94: d_ = h_(d_); break;                         // D=*D
    case 95: d_ = header_[pc_++]; break;                 // D= N
    case 96: m_(b_) = static_cast<u8>(a_); break;        // *B=A
    case 97: m_(b_) = static_cast<u8>(b_); break;        // *B=B
    case 98: m_(b_) = static_cast<u8>(c_); break;        // *B=C
    case 99: m_(b_) = static_cast<u8>(d_); break;        // *B=D
    case 100: break;                                     // *B=*B
    case 101: m_(b_) = m_(c_); break;                    // *B=*C
    case 102: m_(b_) = static_cast<u8>(h_(d_)); break;   // *B=*D
    case 103: m_(b_) = header_[pc_++]; break;            // *B= N
    case 104: m_(c_) = static_cast<u8>(a_); break;       // *C=A
    case 105: m_(c_) = static_cast<u8>(b_); break;       // *C=B
    case 106: m_(c_) = static_cast<u8>(c_); break;       // *C=C
    case 107: m_(c_) = static_cast<u8>(d_); break;       // *C=D
    case 108: m_(c_) = m_(b_); break;                    // *C=*B
    case 109: break;                                     // *C=*C
    case 110: m_(c_) = static_cast<u8>(h_(d_)); break;   // *C=*D
    case 111: m_(c_) = header_[pc_++]; break;            // *C= N
    case 112: h_(d_) = a_; break;                        // *D=A
    case 113: h_(d_) = b_; break;                        // *D=B
    case 114: h_(d_) = c_; break;                        // *D=C
    case 115: h_(d_) = d_; break;                        // *D=D
    case 116: h_(d_) = m_(b_); break;                    // *D=*B
    case 117: h_(d_) = m_(c_); break;                    // *D=*C
    case 118: break;                                     // *D=*D
    case 119: h_(d_) = header_[pc_++]; break;            // *D= N
    case 128: a_ += a_; break;                           // A+=A
    case 129: a_ += b_; break;                           // A+=B
    case 130: a_ += c_; break;                           // A+=C
    case 131: a_ += d_; break;                           // A+=D
    case 132: a_ += m_(b_); break;                       // A+=*B
    case 133: a_ += m_(c_); break;                       // A+=*C
    case 134: a_ += h_(d_); break;                       // A+=*D
    case 135: a_ += header_[pc_++]; break;               // A+= N
    case 136: a_ -= a_; break;                           // A-=A
    case 137: a_ -= b_; break;                           // A-=B
    case 138: a_ -= c_; break;                           // A-=C
    case 139: a_ -= d_; break;                           // A-=D
    case 140: a_ -= m_(b_); break;                       // A-=*B
    case 141: a_ -= m_(c_); break;                       // A-=*C
    case 142: a_ -= h_(d_); break;                       // A-=*D
    case 143: a_ -= header_[pc_++]; break;               // A-= N
    case 144: a_ *= a_; break;                           // A*=A
    case 145: a_ *= b_; break;                           // A*=B
    case 146: a_ *= c_; break;                           // A*=C
    case 147: a_ *= d_; break;                           // A*=D
    case 148: a_ *= m_(b_); break;                       // A*=*B
    case 149: a_ *= m_(c_); break;                       // A*=*C
    case 150: a_ *= h_(d_); break;                       // A*=*D
    case 151: a_ *= header_[pc_++]; break;               // A*= N
    case 152: div(a_); break;                            // A/=A
    case 153: div(b_); break;                            // A/=B
    case 154: div(c_); break;                            // A/=C
    case 155: div(d_); break;                            // A/=D
    case 156: div(m_(b_)); break;                        // A/=*B
    case 157: div(m_(c_)); break;                        // A/=*C
    case 158: div(h_(d_)); break;                        // A/=*D
    case 159: div(header_[pc_++]); break;                // A/= N
    case 160: mod(a_); break;                            // A%=A
    case 161: mod(b_); break;                            // A%=B
    case 162: mod(c_); break;                            // A%=C
    case 163: mod(d_); break;                            // A%=D
    case 164: mod(m_(b_)); break;                        // A%=*B
    case 165: mod(m_(c_)); break;                        // A%=*C
    case 166: mod(h_(d_)); break;                        // A%=*D
    case 167: mod(header_[pc_++]); break;                // A%= N
    case 168: a_ &= a_; break;                           // A&=A
    case 169: a_ &= b_; break;                           // A&=B
    case 170: a_ &= c_; break;                           // A&=C
    case 171: a_ &= d_; break;                           // A&=D
    case 172: a_ &= m_(b_); break;                       // A&=*B
    case 173: a_ &= m_(c_); break;                       // A&=*C
    case 174: a_ &= h_(d_); break;                       // A&=*D
    case 175: a_ &= header_[pc_++]; break;               // A&= N
    case 176: a_ &= ~a_; break;                          // A&~A
    case 177: a_ &= ~b_; break;                          // A&~B
    case 178: a_ &= ~c_; break;                          // A&~C
    case 179: a_ &= ~d_; break;                          // A&~D
    case 180: a_ &= ~m_(b_); break;                      // A&~*B
    case 181: a_ &= ~m_(c_); break;                      // A&~*C
    case 182: a_ &= ~h_(d_); break;                      // A&~*D
    case 183: a_ &= ~header_[pc_++]; break;              // A&~ N
    case 184: a_ |= a_; break;                           // A|=A
    case 185: a_ |= b_; break;                           // A|=B
    case 186: a_ |= c_; break;                           // A|=C
    case 187: a_ |= d_; break;                           // A|=D
    case 188: a_ |= m_(b_); break;                       // A|=*B
    case 189: a_ |= m_(c_); break;                       // A|=*C
    case 190: a_ |= h_(d_); break;                       // A|=*D
    case 191: a_ |= header_[pc_++]; break;               // A|= N
    case 192: a_ ^= a_; break;                           // A^=A
    case 193: a_ ^= b_; break;                           // A^=B
    case 194: a_ ^= c_; break;                           // A^=C
    case 195: a_ ^= d_; break;                           // A^=D
    case 196: a_ ^= m_(b_); break;                       // A^=*B
    case 197: a_ ^= m_(c_); break;                       // A^=*C
    case 198: a_ ^= h_(d_); break;                       // A^=*D
    case 199: a_ ^= header_[pc_++]; break;               // A^= N
    case 200: a_ <<= (a_ & 31); break;                   // A<<=A
    case 201: a_ <<= (b_ & 31); break;                   // A<<=B
    case 202: a_ <<= (c_ & 31); break;                   // A<<=C
    case 203: a_ <<= (d_ & 31); break;                   // A<<=D
    case 204: a_ <<= (m_(b_) & 31); break;               // A<<=*B
    case 205: a_ <<= (m_(c_) & 31); break;               // A<<=*C
    case 206: a_ <<= (h_(d_) & 31); break;               // A<<=*D
    case 207: a_ <<= (header_[pc_++] & 31); break;       // A<<= N
    case 208: a_ >>= (a_ & 31); break;                   // A>>=A
    case 209: a_ >>= (b_ & 31); break;                   // A>>=B
    case 210: a_ >>= (c_ & 31); break;                   // A>>=C
    case 211: a_ >>= (d_ & 31); break;                   // A>>=D
    case 212: a_ >>= (m_(b_) & 31); break;               // A>>=*B
    case 213: a_ >>= (m_(c_) & 31); break;               // A>>=*C
    case 214: a_ >>= (h_(d_) & 31); break;               // A>>=*D
    case 215: a_ >>= (header_[pc_++] & 31); break;       // A>>= N
    case 216: f_ = 1; break;                             // A==A
    case 217: f_ = (a_ == b_); break;                    // A==B
    case 218: f_ = (a_ == c_); break;                    // A==C
    case 219: f_ = (a_ == d_); break;                    // A==D
    case 220: f_ = (a_ == m_(b_)); break;                // A==*B
    case 221: f_ = (a_ == m_(c_)); break;                // A==*C
    case 222: f_ = (a_ == h_(d_)); break;                // A==*D
    case 223: f_ = (a_ == header_[pc_++]); break;        // A== N
    case 224: f_ = 0; break;                             // A<A
    case 225: f_ = (a_ < b_); break;                     // A<B
    case 226: f_ = (a_ < c_); break;                     // A<C
    case 227: f_ = (a_ < d_); break;                     // A<D
    case 228: f_ = (a_ < m_(b_)); break;                 // A<*B
    case 229: f_ = (a_ < m_(c_)); break;                 // A<*C
    case 230: f_ = (a_ < h_(d_)); break;                 // A<*D
    case 231: f_ = (a_ < header_[pc_++]); break;         // A< N
    case 232: f_ = 0; break;                             // A>A
    case 233: f_ = (a_ > b_); break;                     // A>B
    case 234: f_ = (a_ > c_); break;                     // A>C
    case 235: f_ = (a_ > d_); break;                     // A>D
    case 236: f_ = (a_ > m_(b_)); break;                 // A>*B
    case 237: f_ = (a_ > m_(c_)); break;                 // A>*C
    case 238: f_ = (a_ > h_(d_)); break;                 // A>*D
    case 239: f_ = (a_ > header_[pc_++]); break;         // A> N
    case 255:                                            // LJ
      if ((pc_ = hbegin_ + header_[pc_] + 256 * header_[pc_ + 1]) >= hend_)
        err();
      break;
    default: err();
  }
  return 1;
}

} // namespace zpaq_ng::compression