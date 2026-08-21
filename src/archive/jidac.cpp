// jidac.cpp - JIDAC journaling archive layer.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "archive/jidac.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <vector>

#include "compression/compress_block.hpp"
#include "compression/make_config.hpp"
#include "decompression/block_decoder.hpp"
#include "integrity/sha1.hpp"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace zpaq_ng::archive {

// --------------------------- FileIn / FileOut ---------------------------

static FILE* open_file(const char* name, const char* mode) {
#ifdef _WIN32
  // Convert UTF-8 to UTF-16 and use _wfopen.
  const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
  std::vector<wchar_t> w(n);
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, name, -1, w.data(), n);
  return _wfopen(w.data(), (std::wstring(mode, mode + strlen(mode))).c_str());
#else
  return std::fopen(name, mode);
#endif
}

FileIn::FileIn(const char* name) : f_(name ? open_file(name, "rb") : nullptr) {}

int FileIn::get() {
  int c = -1;
  if (f_) c = std::fgetc(f_);
  if (c != -1) ++pos_;
  return c;
}

std::size_t FileIn::read(char* buf, std::size_t n) {
  if (!f_) return 0;
  const std::size_t r = std::fread(buf, 1, n, f_);
  pos_ += r;
  return r;
}

void FileIn::seek(std::int64_t pos) {
  if (f_ && std::fseek(f_, long(pos), SEEK_SET) == 0) pos_ = pos;
}

std::int64_t FileIn::tell() const {
  return f_ ? std::ftell(f_) : -1;
}

void FileIn::close() {
  if (f_) {
    std::fclose(f_);
    f_ = nullptr;
  }
}

FileOut::FileOut(const char* name) {
  f_ = name ? open_file(name, "r+b") : nullptr;
  if (!f_ && name) f_ = open_file(name, "wb");
}

void FileOut::put(int c) {
  if (f_) {
    std::fputc(c & 255, f_);
    ++pos_;
  }
}

void FileOut::write(const char* buf, std::size_t n) {
  if (f_) {
    std::fwrite(buf, 1, n, f_);
    pos_ += n;
  }
}

void FileOut::seek(std::int64_t pos) {
  if (f_ && std::fseek(f_, long(pos), SEEK_SET) == 0) pos_ = pos;
}

void FileOut::seek_end() {
  if (f_ && std::fseek(f_, 0, SEEK_END) == 0) pos_ = std::ftell(f_);
}

std::int64_t FileOut::tell() const {
  return f_ ? std::ftell(f_) : -1;
}

std::int64_t FileOut::size() const {
  if (!f_) return -1;
  const long cur = std::ftell(f_);
  std::fseek(f_, 0, SEEK_END);
  const long end = std::ftell(f_);
  std::fseek(f_, cur, SEEK_SET);
  return end;
}

void FileOut::truncate(std::int64_t size) {
#ifdef _WIN32
  if (f_ && _chsize_s(_fileno(f_), size) != 0) {
    fprintf(stderr, "cannot truncate archive\n");
  }
#else
  if (f_ && ftruncate(fileno(f_), size) != 0) {
    fprintf(stderr, "cannot truncate archive\n");
  }
#endif
}

void FileOut::flush() {
  if (f_) std::fflush(f_);
}

void FileOut::close() {
  if (f_) {
    std::fclose(f_);
    f_ = nullptr;
  }
}

// ------------------------------ utilities ------------------------------

std::string itoa64(std::int64_t x, int n) {
  assert(x >= 0);
  assert(n >= 0);
  std::string r;
  for (; x || n > 0; x /= 10, --n) r = std::string(1, char('0' + x % 10)) + r;
  return r;
}

std::int64_t decimal_time(std::int64_t tt) {
  if (tt == -1) tt = 0;
  std::int64_t t = tt;
  const int second = t % 60;
  const int minute = t / 60 % 60;
  const int hour = t / 3600 % 24;
  t /= 86400;  // days since Jan 1 1970
  const int term = t / 1461;  // 4 year terms since 1970
  t %= 1461;
  t += (t >= 59);  // insert Feb 29 on non leap years
  t += (t >= 425);
  t += (t >= 1157);
  const int year = term * 4 + t / 366 + 1970;
  t %= 366;
  t += (t >= 60) * 2;  // make Feb. 31 days
  t += (t >= 123);     // insert Apr 31
  t += (t >= 185);     // insert June 31
  t += (t >= 278);     // insert Sept 31
  t += (t >= 340);     // insert Nov 31
  const int month = t / 31 + 1;
  const int day = t % 31 + 1;
  return year * 10000000000LL + month * 100000000 + day * 1000000 +
         hour * 10000 + minute * 100 + second;
}

std::int64_t unix_time(std::int64_t date) {
  if (date <= 0) return -1;
  static const int days[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const int year = date / 10000000000LL % 10000;
  const int month = (date / 100000000 % 100 - 1) % 12;
  const int day = date / 1000000 % 100;
  const int hour = date / 10000 % 100;
  const int min = date / 100 % 100;
  const int sec = date % 100;
  return (day - 1 + days[month] + (year % 4 == 0 && month > 1) + ((year - 1970) * 1461 + 1) / 4) *
             86400 +
         hour * 3600 + min * 60 + sec;
}

// Convert 4 byte little-endian int and advance s.
static unsigned btoi(const char*& s) {
  s += 4;
  return (unsigned char)s[-4] | ((unsigned char)s[-3] << 8) |
         ((unsigned char)s[-2] << 16) | ((unsigned char)s[-1] << 24);
}

// Convert 8 byte little-endian int and advance s.
static std::int64_t btol(const char*& s) {
  const std::uint64_t r = btoi(s);
  return r + (std::uint64_t(btoi(s)) << 32);
}

// Append n bytes of x to sb in LSB order.
static void puti(io::Buffer& sb, std::uint64_t x, int n) {
  for (; n > 0; --n) sb.put(int(x & 255)), x >>= 8;
}

static bool exists(const std::string& name) {
  FILE* f = open_file(name.c_str(), "rb");
  if (f) {
    std::fclose(f);
    return true;
  }
  return false;
}

// In Windows convert upper case to lower case.
inline int tolowerW(int c) {
#ifdef _WIN32
  if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
#endif
  return c;
}

// Return true if strings a == b or a+"/" is a prefix of b, or a ends in "/"
// and is a prefix of b. Match ? in a to any char in b and * to any string.
bool ispath(const char* a, const char* b) {
  for (; *a; ++a, ++b) {
    const int ca = tolowerW(*a);
    const int cb = tolowerW(*b);
    if (ca == '*') {
      while (true) {
        if (ispath(a + 1, b)) return true;
        if (!*b) return false;
        ++b;
      }
    } else if (ca == '?') {
      if (*b == 0) return false;
    } else if (ca == cb && ca == '/' && a[1] == 0)
      return true;
    else if (ca != cb)
      return false;
  }
  return *b == 0 || *b == '/';
}

std::string append_path(std::string a, std::string b) {
  int na = int(a.size());
  int nb = int(b.size());
#ifdef _WIN32
  if (nb > 1 && b[1] == ':') {  // remove : from drive letter
    if (nb > 2 && b[2] != '/') b[1] = '/';
    else b = b[0] + b.substr(2), --nb;
  }
#endif
  if (nb > 0 && b[0] == '/') b = b.substr(1);
  return (na > 0 && a[na - 1] == '/') ? a + b : a + "/" + b;
}

// Return the part of fn up to the last slash.
static std::string path(const std::string& fn) {
  int n = 0;
  for (int i = 0; fn[i]; ++i)
    if (fn[i] == '/' || fn[i] == '\\') n = i + 1;
  return fn.substr(0, n);
}

// Make a directory, or no-op if it exists. Creates parents.
static void makepath(const std::string& filename) {
  const std::string dir = path(filename);
  std::string d;
  for (char c : dir) {
    if (c == '/' || c == '\\') {
      if (!d.empty()) {
#ifdef _WIN32
        _mkdir(d.c_str());
#else
        mkdir(d.c_str(), 0777);
#endif
      }
    }
    d += c;
  }
#ifdef _WIN32
  if (!d.empty()) _mkdir(d.c_str());
#else
  if (!d.empty()) mkdir(d.c_str(), 0777);
#endif
}

// ------------------------------ read_archive ------------------------------

std::int64_t Jidac::read_archive(const char* arc, int* errors) {
  if (errors) *errors = 0;
  dcsize = dhsize = 0;
  assert(ver.size() == 1);
  unsigned files = 0;  // count
  ht.resize(1);        // element 0 not used

  FileIn in(arc);
  if (!in.is_open()) {
    if (command != 'a') {
      fprintf(stderr, "%s not found.\n", arc);
      if (errors) ++*errors;
    }
    return 0;
  }
  printf("%s", arc);
  if (version == 99999999999999LL) printf(": ");
  else printf(" -until %1.0f: ", version + 0.0);
  fflush(stdout);

  // Scan archive contents
  std::string lastfile = archive;  // last named file in streaming format
  if (lastfile.size() > 5 && lastfile.substr(lastfile.size() - 5) == ".zpaq")
    lastfile = lastfile.substr(0, lastfile.size() - 5);
  std::int64_t block_offset = 0;  // start of last block of any type
  std::int64_t data_offset = 0;   // start of last block of d fragments
  bool found_data = false;
  bool first = true;  // first segment in archive?
  io::Buffer os;      // decompressed block
  const bool renamed = command == 'l' || command == 'a';

  bool done = false;
  while (!done) {
    decompression::BlockDecoder d;
    try {
      d.set_input(&in);
      while (d.find_block()) {
        found_data = true;

        io::MemoryWriter filename, comment;
        int segs = 0;  // segments in block
        bool skip = false;
        while (d.find_filename(&filename)) {
          std::string fs = std::string(filename.bytes().begin(), filename.bytes().end());
          if (fs.size()) {
            for (char& ch : fs)
              if (ch == '\\') ch = '/';
            lastfile = fs.c_str();
          }
          comment.clear();
          d.read_comment(&comment);
          std::string cs = std::string(comment.bytes().begin(), comment.bytes().end());

          // Test for JIDAC format. Filename is jDC<fdate>[cdhi]<num> and
          // comment ends with " jDC\x01".
          if (cs.size() >= 4 && cs.substr(cs.size() - 4) == "jDC\x01") {
            if (fs.size() != 28 || fs.substr(0, 3) != "jDC")
              throw format_error("bad journaling block name");
            if (skip) throw format_error("mixed journaling and streaming block");

            // Read uncompressed size from comment.
            std::int64_t usize = 0;
            unsigned i;
            for (i = 0; i < cs.size() && std::isdigit(cs[i]); ++i) {
              usize = usize * 10 + cs[i] - '0';
              if (usize > 0xffffffff) throw format_error("journaling block too big");
            }

            // Read the date and number in the filename.
            std::int64_t fdate = 0, num = 0;
            for (i = 3; i < 17 && std::isdigit(fs[i]); ++i)
              fdate = fdate * 10 + fs[i] - '0';
            if (i != 17 || fdate < 19000000000000LL || fdate >= 30000000000000LL)
              throw format_error("bad date");
            for (i = 18; i < 28 && std::isdigit(fs[i]); ++i)
              num = num * 10 + fs[i] - '0';
            if (i != 28 || num > 0xffffffff) throw format_error("bad fragment");

            // Decompress the block.
            os.reset();
            d.set_output(&os);
            integrity::SHA1 sha1;
            d.set_sha1(&sha1);
            if (std::strchr("chi", fs[17])) {
              d.decompress();
              char sha1result[21] = {0};
              d.read_segment_end(sha1result);
              if ((std::int64_t)os.size() != usize) throw format_error("bad block size");
              if (usize != std::int64_t(sha1.usize())) throw format_error("bad checksum size");
              if (sha1result[0] && std::memcmp(sha1result + 1, sha1.result(), 20))
                throw format_error("bad checksum");
            } else
              d.read_segment_end();

            // Transaction header (type c).
            if (fs[17] == 'c') {
              if (os.size() < 8) throw format_error("c block too small");
              data_offset = in.tell() + 1 - d.buffered();
              const char* s = reinterpret_cast<const char*>(os.data());
              std::int64_t jmp = btol(s);
              if (jmp < 0) printf("Incomplete transaction ignored\n");
              if (jmp < 0 ||
                  (version < 19000000000000LL && std::int64_t(ver.size()) > version) ||
                  (version >= 19000000000000LL && version < fdate)) {
                done = true;
                goto endblock;
              } else {
                dcsize += jmp;
                if (jmp) in.seek(data_offset + jmp);
                ver.push_back(VER());
                ver.back().firstFragment = unsigned(ht.size());
                ver.back().offset = block_offset;
                ver.back().data_offset = data_offset;
                ver.back().date = ver.back().lastdate = fdate;
                ver.back().csize = jmp;
                if (all) {
                  std::string fn = itoa64(ver.size() - 1, all) + "/";
                  if (renamed) fn = rename(fn);
                  if (isselected(fn.c_str(), false)) dt[fn].date = fdate;
                }
                if (jmp) goto endblock;
              }
            }

            // Fragment table (type h).
            else if (fs[17] == 'h') {
              assert(ver.size() > 0);
              if (fdate > ver.back().lastdate) ver.back().lastdate = fdate;
              if (os.size() % 24 != 4) throw format_error("bad h block size");
              const unsigned n = unsigned((os.size() - 4) / 24);
              if (num < 1 || num + n > 0xffffffff) throw format_error("bad h fragment");
              const char* s = reinterpret_cast<const char*>(os.data());
              const unsigned bsize = btoi(s);
              dhsize += bsize;
              assert(ver.size() > 0);
              if (std::int64_t(ht.size()) > num) {
                fprintf(stderr,
                        "Unordered fragment tables: expected >= %d found %1.0f\n",
                        int(ht.size()), double(num));
              }
              for (unsigned i = 0; i < n; ++i) {
                if (i == 0) {
                  block.push_back(Block(num, data_offset));
                  block.back().usize = 8;
                  block.back().bsize = bsize;
                  block.back().frags = unsigned(os.size() / 24);
                }
                while (std::int64_t(ht.size()) <= num + i) ht.push_back(HT());
                std::memcpy(ht[num + i].sha1, s, 20);
                s += 20;
                assert(block.size() > 0);
                const unsigned f = btoi(s);
                if (f > 0x7fffffff) throw format_error("fragment too big");
                block.back().usize += (ht[num + i].usize = int(f)) + 4u;
              }
              data_offset += bsize;
            }

            // Index (type i).
            else if (fs[17] == 'i') {
              assert(ver.size() > 0);
              if (fdate > ver.back().lastdate) ver.back().lastdate = fdate;
              const char* s = reinterpret_cast<const char*>(os.data());
              const char* const end = s + os.size();
              while (s + 9 <= end) {
                DT dtr;
                dtr.date = btol(s);  // date
                if (dtr.date) ++ver.back().updates;
                else ++ver.back().deletes;
                const std::int64_t len = std::strlen(s);
                if (len > 65535) throw format_error("filename too long");
                std::string fn = s;
                if (all) fn = append_path(itoa64(ver.size() - 1, all), fn);
                const bool issel = isselected(fn.c_str(), renamed);
                s += len + 1;
                if (s > end) throw format_error("filename too long");
                if (dtr.date) {
                  ++files;
                  if (s + 4 > end) throw format_error("missing attr");
                  const unsigned na = btoi(s);
                  if (s + na > end || na > 65535) throw format_error("attr too long");
                  for (unsigned i = 0; i < na; ++i, ++s)
                    if (i < 8) dtr.attr += std::int64_t(*s & 255) << (i * 8);
                  if (noattributes) dtr.attr = 0;
                  if (s + 4 > end) throw format_error("missing ptr");
                  const unsigned ni = btoi(s);
                  if (ni > (end - s) / 4u) throw format_error("ptr list too long");
                  if (issel) dtr.ptr.resize(ni);
                  for (unsigned i = 0; i < ni; ++i) {
                    const unsigned j = btoi(s);
                    if (issel) dtr.ptr[i] = j;
                  }
                }
                if (issel) dt[fn] = dtr;
              }
            } else {
              printf("Skipping %s %s\n", fs.c_str(), cs.c_str());
              throw format_error("Unexpected journaling block");
            }
          } else {  // streaming format
            if (ver.size() == 1) {
              if (version < 1) {
                done = true;
                goto endblock;
              }
              ver.push_back(VER());
              ver.back().firstFragment = unsigned(ht.size());
              ver.back().offset = block_offset;
              ver.back().csize = -1;
            }
            char sha1result[21] = {0};
            d.read_segment_end(sha1result);
            skip = true;
            std::string fn = lastfile;
            if (all) fn = append_path(itoa64(ver.size() - 1, all), fn);
            if (isselected(fn.c_str(), renamed)) {
              DT& dtr = dt[fn];
              if (fs.size() > 0 || first) {
                ++files;
                dtr.date = date;
                dtr.attr = 0;
                dtr.ptr.resize(0);
                ++ver.back().updates;
              }
              dtr.ptr.push_back(unsigned(ht.size()));
            }
            assert(ver.size() > 0);
            if (segs == 0 || block.size() == 0)
              block.push_back(Block(unsigned(ht.size()), block_offset));
            assert(block.size() > 0);
            ht.push_back(HT(sha1result + 1, -1));
          }
          ++segs;
          first = false;
        }  // end while find_filename
        if (!done) block_offset = in.tell() - d.buffered();
      }  // end while find_block
      done = true;
    }  // end try
    catch (std::exception& e) {
      in.seek(in.tell() - d.buffered());
      fprintf(stderr, "Skipping block at %1.0f: %s\n", double(block_offset), e.what());
      if (errors) ++*errors;
    }
  endblock:;
  }  // end while !done
  if (in.tell() > 0 && !found_data) throw format_error("archive contains no data");
  printf("%d versions, %u files, %u fragments, %1.6f MB\n", int(ver.size()) - 1, files,
         unsigned(ht.size()) - 1, block_offset / 1000000.0);

  // Calculate file sizes
  for (DTMap::iterator p = dt.begin(); p != dt.end(); ++p) {
    for (unsigned i = 0; i < p->second.ptr.size(); ++i) {
      unsigned j = p->second.ptr[i];
      if (j > 0 && j < ht.size() && p->second.size >= 0) {
        if (ht[j].usize >= 0) p->second.size += ht[j].usize;
        else p->second.size = -1;
      }
    }
  }
  return block_offset;
}

// Test whether filename and attributes are selected by files, -only, -not.
bool Jidac::isselected(const char* filename, bool rn) {
  bool matched = true;
  if (files.size() > 0) {
    matched = false;
    for (unsigned i = 0; i < files.size() && !matched; ++i) {
      if (rn && i < tofiles.size()) {
        if (ispath(tofiles[i].c_str(), filename)) matched = true;
      } else if (ispath(files[i].c_str(), filename)) matched = true;
    }
  }
  if (!matched) return false;
  if (onlyfiles.size() > 0) {
    matched = false;
    for (unsigned i = 0; i < onlyfiles.size() && !matched; ++i)
      if (ispath(onlyfiles[i].c_str(), filename)) matched = true;
  }
  if (!matched) return false;
  for (unsigned i = 0; i < notfiles.size(); ++i)
    if (ispath(notfiles[i].c_str(), filename)) return false;
  return true;
}

// Insert external filename into dt if selected. If filename is a directory
// then also insert its contents. In Windows, filename might have wildcards.
void Jidac::scandir(const std::string& filename) {
#ifdef _WIN32
  // Expand wildcards
  std::string t = filename;
  if (t.size() > 0 && t[t.size() - 1] == '/') t += "*";
  WIN32_FIND_DATAW ffd;
  const int wn = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, nullptr, 0);
  std::vector<wchar_t> w(wn);
  if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, w.data(), wn);
  HANDLE h = FindFirstFileW(w.data(), &ffd);
  while (h != INVALID_HANDLE_VALUE) {
    std::int64_t edate = 0;
    SYSTEMTIME st;
    if (FileTimeToSystemTime(&ffd.ftLastWriteTime, &st))
      edate = st.wYear * 10000000000LL + st.wMonth * 100000000LL +
              st.wDay * 1000000 + st.wHour * 10000 + st.wMinute * 100 + st.wSecond;
    const std::int64_t esize = ffd.nFileSizeLow + (std::int64_t(ffd.nFileSizeHigh) << 32);
    const std::int64_t eattr = 'w' + (std::int64_t(ffd.dwFileAttributes) << 8);

    const int fn = WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> fname(fn);
    if (fn > 0) WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, fname.data(), fn, nullptr, nullptr);
    std::string name = fname.data();
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT || name == "." || name == "..")
      edate = 0;  // don't add
    std::string fn2 = path(filename) + name;

    if (edate) {
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) fn2 += "/";
      addfile(fn2, edate, esize, eattr);
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        fn2 += "*";
        scandir(fn2);
      }
    }
    if (!FindNextFileW(h, &ffd)) break;
  }
  if (h != INVALID_HANDLE_VALUE) FindClose(h);
#else
  (void)filename;
#endif
}

// Add external file and its date, size, and attributes to dt.
void Jidac::addfile(std::string filename, std::int64_t edate, std::int64_t esize,
                    std::int64_t eattr) {
  if (!isselected(filename.c_str(), false)) return;
  DT& d = edt[filename];
  d.date = edate;
  d.size = esize;
  d.attr = noattributes ? 0 : eattr;
  d.data = 0;
}

// Rename from -to.
std::string Jidac::rename(const std::string& name) {
  if (tofiles.size() == 0) return name;
  if (tofiles.size() == 1) return append_path(tofiles[0], name);
  for (unsigned i = 0; i < tofiles.size() && i < files.size(); ++i) {
    if (ispath(files[i].c_str(), name.c_str())) return append_path(tofiles[i], name);
  }
  return name;
}

// Return true if the file on disk matches the archived fragments.
bool Jidac::equal(DTMap::const_iterator p, const char* filename) {
  // test if all fragment sizes and hashes exist
  if (filename == nullptr) {
    static const char zero[20] = {0};
    for (unsigned i = 0; i < p->second.ptr.size(); ++i) {
      const unsigned j = p->second.ptr[i];
      if (j < 1 || j >= ht.size() || ht[j].usize < 0 ||
          !std::memcmp(ht[j].sha1, zero, 20))
        return false;
    }
    return true;
  }

  // internal or neither file exists
  if (p->second.date == 0) return !exists(filename);

  // directories always match
  if (p->first != "" && p->first[p->first.size() - 1] == '/')
    return exists(filename);

  // compare sizes
  FILE* in = open_file(filename, "rb");
  if (!in) return false;
  std::fseek(in, 0, SEEK_END);
  if (std::ftell(in) != p->second.size) return std::fclose(in), false;

  // compare hashes
  std::fseek(in, 0, SEEK_SET);
  integrity::SHA1 sha1;
  const int BUFSIZE = 4096;
  char buf[BUFSIZE];
  for (unsigned i = 0; i < p->second.ptr.size(); ++i) {
    const unsigned f = p->second.ptr[i];
    if (f < 1 || f >= ht.size() || ht[f].usize < 0) return std::fclose(in), false;
    for (int j = 0; j < ht[f].usize;) {
      int n = ht[f].usize - j;
      if (n > BUFSIZE) n = BUFSIZE;
      const int r = int(std::fread(buf, 1, n, in));
      if (r != n) return std::fclose(in), false;
      sha1.write(buf, n);
      j += n;
    }
    if (std::memcmp(sha1.result(), ht[f].sha1, 20) != 0) return std::fclose(in), false;
  }
  if (std::fread(buf, 1, BUFSIZE, in) != 0) return std::fclose(in), false;
  std::fclose(in);
  return true;
}

// Set date and attributes on an existing file.
void Jidac::close_file(const char* filename, std::int64_t date, std::int64_t attr) {
  assert(filename);
#ifdef _WIN32
  const bool ads = strstr(filename, ":$DATA") != 0;  // alternate data stream?
  if (date > 0 && !ads) {
    HANDLE h = CreateFileA(filename, FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      SYSTEMTIME st;
      st.wYear = WORD(date / 10000000000LL % 10000);
      st.wMonth = WORD(date / 100000000 % 100);
      st.wDayOfWeek = 0;  // ignored
      st.wDay = WORD(date / 1000000 % 100);
      st.wHour = WORD(date / 10000 % 100);
      st.wMinute = WORD(date / 100 % 100);
      st.wSecond = WORD(date % 100);
      st.wMilliseconds = 0;
      FILETIME ft;
      SystemTimeToFileTime(&st, &ft);
      SetFileTime(h, nullptr, nullptr, &ft);
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
  if ((attr & 255) == 'w' && !ads) SetFileAttributesA(filename, DWORD(attr >> 8));
#else
  (void)filename;
  (void)date;
  (void)attr;
#endif
}

// Maps sha1 -> fragment ID in ht with known size.
namespace {
class HTIndex {
  std::vector<HT>& htr;
  std::vector<unsigned> t;
  unsigned htsize;

  unsigned hash(const unsigned char* sha1) {
    unsigned h = 0;
    for (int i = 0; i < 4; ++i) h = (h << 8) | sha1[i];
    return h & (t.size() - 1);
  }

public:
  HTIndex(std::vector<HT>& r, size_t sz) : htr(r), htsize(1) {
    int b;
    for (b = 1; sz * 3 >> b; ++b)
      ;
    t.resize(1, b - 1);
    update();
  }

  unsigned find(const unsigned char* sha1) {
    const unsigned h = hash(sha1);
    for (unsigned i = 0; i < t.size(); ++i) {
      if (t[h ^ i] == 0) return 0;
      if (std::memcmp(sha1, htr[t[h ^ i]].sha1, 20) == 0) return t[h ^ i];
    }
    return 0;
  }

  void update() {
    static const unsigned char zero[20] = {0};
    while (htsize < htr.size()) {
      if (htsize >= t.size() / 4 * 3) {
        t.resize(t.size() * 2);
        htsize = 1;
      }
      if (htr[htsize].usize >= 0 && std::memcmp(htr[htsize].sha1, zero, 20) != 0) {
        const unsigned h = hash(htr[htsize].sha1);
        for (unsigned i = 0; i < t.size(); ++i) {
          if (t[h ^ i] == 0) {
            t[h ^ i] = htsize;
            break;
          }
        }
      }
      ++htsize;
    }
  }
};

// Sort by sortkey, then by full path.
bool compareFilename(DTMap::iterator ap, DTMap::iterator bp) {
  if (ap->second.data != bp->second.data) return ap->second.data < bp->second.data;
  return ap->first < bp->first;
}
}  // namespace

// Write a ZPAQ compressed JIDAC block header. Output size should not
// depend on input data.
static void writeJidacHeader(io::Writer* out, std::int64_t date, std::int64_t cdata,
                             unsigned htsize) {
  if (!out) return;
  assert(date >= 19000000000000LL && date < 30000000000000LL);
  io::Buffer is;
  puti(is, cdata, 8);
  compression::compress_block(&is, out, "0",
                              ("jDC" + itoa64(date, 14) + "c" + itoa64(htsize, 10)).c_str(),
                              "jDC\x01", true);
}

// -------------------------------- add ---------------------------------

int Jidac::add() {
  int errors = 0;
  std::string arcname = archive;
  std::int64_t header_pos = 0;
  if (exists(arcname.c_str())) header_pos = read_archive(arcname.c_str(), &errors);
  else read_archive(arcname.c_str(), &errors);

  if (exists(arcname.c_str())) printf("Updating ");
  else printf("Creating ");
  printf("%s at offset %1.0f\n", arcname.c_str(), double(header_pos));

  // Set method.
  if (method == "") method = "1";
  if (method.size() == 1) {  // set default blocksize
    if (method[0] >= '2' && method[0] <= '9') method += "6";
    else method += "4";
  }
  if (std::strchr("0123456789xs", method[0]) == 0)
    throw invalid_argument_error("-method must begin with 0..5, x, s");
  assert(method.size() >= 2);

  // Set block and fragment sizes.
  if (fragment < 0) fragment = 0;
  const int log_blocksize = 20 + std::atoi(method.c_str() + 1);
  if (log_blocksize < 20 || log_blocksize > 31)
    throw invalid_argument_error("blocksize must be 0..11");
  const unsigned blocksize = (1u << log_blocksize) - 4096;
  const unsigned MAX_FRAGMENT =
      fragment > 19 || (8128u << fragment) > blocksize - 12 ? blocksize - 12 : 8128u << fragment;
  const unsigned MIN_FRAGMENT =
      fragment > 25 || (64u << fragment) > MAX_FRAGMENT ? MAX_FRAGMENT : 64u << fragment;

  // Don't mix streaming and journaling.
  for (unsigned i = 0; i < block.size(); ++i) {
    if (method[0] == 's') {
      if (block[i].usize >= 0) throw format_error("cannot update journaling archive in streaming format");
    } else if (block[i].usize < 0)
      throw format_error("cannot update streaming archive in journaling format");
  }

  // Make list of files to add or delete.
  for (unsigned i = 0; i < files.size(); ++i) scandir(files[i].c_str());

  // Sort the files to be added by filename extension and decreasing size.
  std::vector<DTMap::iterator> vf;
  std::int64_t total_size = 0;
  for (DTMap::iterator p = edt.begin(); p != edt.end(); ++p) {
    DTMap::iterator a = dt.find(rename(p->first));
    if (a != dt.end()) a->second.data = 1;  // keep
    if (p->second.date && p->first != "" && p->first[p->first.size() - 1] != '/' &&
        (force || a == dt.end() || p->second.date != a->second.date ||
         p->second.size != a->second.size)) {
      total_size += p->second.size;

      // Key by first 5 bytes of filename extension, case insensitive.
      int sp = 0;
      for (std::string::const_iterator q = p->first.begin(); q != p->first.end(); ++q) {
        std::uint64_t c = *q & 255;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '/') sp = 0, p->second.data = 0;
        else if (c == '.') sp = 8, p->second.data = 0;
        else if (sp > 3) p->second.data += c << (--sp * 8);
      }

      // Key by descending size rounded to 16K.
      std::int64_t s = p->second.size >> 14;
      if (s >= (1 << 24)) s = (1 << 24) - 1;
      p->second.data += (1 << 24) - s - 1;
      vf.push_back(p);
    }
  }
  std::sort(vf.begin(), vf.end(), compareFilename);

  // Open output.
  FileOut out(arcname.c_str());
  if (!out.is_open()) throw std::runtime_error("cannot open archive for writing");
  out.seek(header_pos);

  printf("Adding %1.6f MB in %d files -method %s at %s.\n", total_size / 1000000.0,
         int(vf.size()), method.c_str(), itoa64(date).c_str());

  // Streaming mode: each file is a separate block.
  std::int64_t dedupesize = 0;
  if (method[0] == 's') {
    io::Buffer sb;
    for (unsigned fi = 0; fi < vf.size(); ++fi) {
      DTMap::iterator p = vf[fi];
      printf("+ %s %1.0f\n", p->first.c_str(), p->second.size + 0.0);
      FileIn in(p->first.c_str());
      if (!in.is_open()) {
        fprintf(stderr, "%s not found\n", p->first.c_str());
        total_size -= p->second.size;
        ++errors;
        continue;
      }
      std::uint64_t i = 0;
      char buf[4096];
      while (true) {
        const int r = int(in.read(buf, sizeof buf));
        sb.write(buf, r);
        i += r;
        if (r == 0 || sb.size() + 4096 > blocksize) {
          std::string filename = "";
          std::string comment = "";
          if (i == sb.size()) {
            filename = rename(p->first);
            comment = itoa64(p->second.date);
            if ((p->second.attr & 255) > 0) {
              comment += " ";
              comment += char(p->second.attr & 255);
              comment += itoa64(p->second.attr >> 8);
            }
          }
          compression::compress_block(&sb, &out, method.c_str(), filename.c_str(),
                                      comment.c_str(), true);
          assert(sb.size() == 0);
        }
        if (r == 0) break;
      }
    }
    const std::int64_t outsize = out.tell();
    printf("%1.0f + (%1.0f -> %1.0f) = %1.0f\n", double(header_pos), double(total_size),
           double(outsize - header_pos), double(outsize));
    out.flush();
    return errors > 0;
  }

  // Adjust date to maintain sequential order.
  if (ver.size() && ver.back().lastdate >= date) {
    const std::int64_t newdate = decimal_time(unix_time(ver.back().lastdate) + 1);
    fprintf(stderr, "Warning: adjusting date from %s to %s\n", itoa64(date).c_str(),
            itoa64(newdate).c_str());
    assert(newdate > date);
    date = newdate;
  }

  // Build htinv for fast lookups of sha1 in ht.
  HTIndex htinv(ht, ht.size() + (total_size >> (10 + fragment)) + vf.size());
  const unsigned htsize = unsigned(ht.size());  // fragments at start of update

  // Reserve space for the header block.
  writeJidacHeader(&out, date, -1, htsize);
  const std::int64_t header_end = out.tell();

  // Compress until end of last file.
  io::Buffer sb;
  unsigned frags = 0;       // number of fragments in sb
  unsigned redundancy = 0;  // estimated bytes that can be compressed out of sb
  unsigned text = 0;        // number of fragments containing text
  unsigned exe = 0;         // number of fragments containing x86 (exe, dll)
  const int ON = 4;         // number of order-1 tables to save
  unsigned char o1prev[ON * 256] = {0};
  std::vector<unsigned char> fragbuf(MAX_FRAGMENT);
  std::vector<unsigned> blocklist;  // list of starting fragments

  for (unsigned fi = 0; fi <= vf.size(); ++fi) {
    FileIn in(nullptr);
    char buf[4096];
    int bufptr = 0, buflen = 0;
    if (fi < vf.size()) {
      assert(vf[fi]->second.ptr.size() == 0);
      DTMap::iterator p = vf[fi];
      bufptr = buflen = 0;
      in = FileIn(p->first.c_str());
      if (!in.is_open()) {  // skip if not found
        p->second.date = 0;
        total_size -= p->second.size;
        fprintf(stderr, "%s not found\n", p->first.c_str());
        ++errors;
        continue;
      }
      p->second.data = 1;  // add
    }

    // Read fragments.
    std::int64_t fsize = 0;
    for (unsigned fj = 0; true; ++fj) {
      std::int64_t sz = 0;  // fragment size
      unsigned hits = 0;
      int c = -1;
      unsigned htptr = 0;  // fragment index
      char sha1result[20] = {0};
      unsigned char o1[256] = {0};
      if (fi < vf.size()) {
        int c1 = 0;  // previous byte
        unsigned h = 0;  // rolling hash
        integrity::SHA1 sha1;
        while (true) {
          if (bufptr >= buflen) bufptr = 0, buflen = int(in.read(buf, sizeof buf));
          if (bufptr >= buflen) c = -1;
          else c = (unsigned char)buf[bufptr++];
          if (c != -1) {
            if (c == o1[c1]) h = (h + c + 1) * 314159265u, ++hits;
            else h = (h + c + 1) * 271828182u;
            o1[c1] = c;
            c1 = c;
            sha1.put(c);
            fragbuf[sz++] = c;
          }
          if (c == -1 || sz >= MAX_FRAGMENT ||
              (fragment <= 22 && h < (1u << (22 - fragment)) && sz >= MIN_FRAGMENT))
            break;
        }
        assert(sz <= MAX_FRAGMENT);
        assert(std::uint64_t(sz) == sha1.usize());
        std::memcpy(sha1result, sha1.result(), 20);
        htptr = htinv.find(reinterpret_cast<const unsigned char*>(sha1result));
      }

      if (htptr == 0) {  // not matched or last block
        int text1 = 0, exe1 = 0;
        std::int64_t h1 = sz;
        unsigned char o1ct[256] = {0};
        static const unsigned char dt[256] = {
            160, 80, 53, 40, 32, 26, 22, 20, 17, 16, 14, 13, 12, 11, 10, 10, 9,  8,  8,  8,  7,  7,  6,  6,  6,  6,  5,  5,  5,  5,  5,  5,
            4,   4,  4,  4,  4,  4,  4,  4,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
            2,   2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
            1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
            1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1};
        for (int i = 0; i < 256; ++i) {
          if (o1ct[o1[i]] < 255) h1 -= (sz * dt[o1ct[o1[i]]++]) >> 15;
          if (o1[i] == ' ' && (std::isalnum(i) || i == '.' || i == ',')) ++text1;
          if (o1[i] && (i < 9 || i == 11 || i == 12 || (i >= 14 && i <= 31) || i >= 240)) --text1;
          if (i >= 192 && i < 240 && o1[i] && (o1[i] < 128 || o1[i] >= 192)) --text1;
          if (o1[i] == 139) ++exe1;
        }
        text1 = (text1 >= 3);
        exe1 = (exe1 >= 5);
        if (sz > 0) h1 = h1 * h1 / sz;
        unsigned h2 = unsigned(h1);
        if (h2 > hits) hits = h2;
        h2 = o1ct[0] * unsigned(sz) / 256;
        if (h2 > hits) hits = h2;
        h2 = 0;
        for (int i = 0; i < 256 * ON; ++i) h2 += o1prev[i] == o1[i & 255];
        h2 = h2 * unsigned(sz) / (256 * ON);
        if (h2 > hits) hits = h2;
        if (hits > sz) hits = unsigned(sz);

        // Start a new block if the current block is almost full, or at the
        // start of a file that won't fit or doesn't share mutual information.
        bool newblock = false;
        if (frags > 0 && fj == 0 && fi < vf.size()) {
          const std::int64_t esize = vf[fi]->second.size;
          const std::int64_t newsize = sb.size() + esize + (esize >> 14) + 4096 + frags * 4;
          if (newsize > blocksize / 4 && redundancy < sb.size() / 128) newblock = true;
          if (newblock) {  // test for mutual information
            unsigned ct = 0;
            for (unsigned i = 0; i < 256 * ON; ++i)
              if (o1prev[i] && o1prev[i] == o1[i & 255]) ++ct;
            if (ct > ON * 2) newblock = false;
          }
          if (newsize >= blocksize) newblock = true;
        }
        if (sb.size() + sz + 80 + frags * 4 >= blocksize) newblock = true;
        if (fi == vf.size()) newblock = true;
        if (frags < 1) newblock = false;

        // Pad sb with fragment size list, then compress.
        if (newblock) {
          assert(frags > 0);
          assert(frags < ht.size());
          for (unsigned i = unsigned(ht.size()) - frags; i < ht.size(); ++i)
            puti(sb, ht[i].usize, 4);  // list of frag sizes
          puti(sb, 0, 4);              // omit first frag ID to make block movable
          puti(sb, frags, 4);          // number of frags
          std::string m = method;
          if (std::isdigit(method[0]))
            m += "," + itoa64(redundancy / (sb.size() / 256 + 1)) + "," +
                 itoa64((exe > frags) * 2 + (text > frags));
          std::string fn = "jDC" + itoa64(date, 14) + "d" + itoa64(ht.size() - frags, 10);
          printf("[%u..%u] %u -method %s\n", unsigned(ht.size()) - frags,
                 unsigned(ht.size()) - 1, unsigned(sb.size()), m.c_str());
          if (method[0] != 'i') {
            const std::int64_t before = out.tell();
            compression::compress_block(&sb, &out, m.c_str(), fn.c_str(), "jDC\x01", true);
            csize_list_.push_back(out.tell() - before);
          } else {
            csize_list_.push_back(std::int64_t(sb.size()));
            sb.reset();
          }
          assert(sb.size() == 0);
          blocklist.push_back(unsigned(ht.size()) - frags);  // mark block start
          frags = redundancy = text = exe = 0;
          std::memset(o1prev, 0, sizeof o1prev);
        }

        // Append fragbuf to sb and update block statistics.
        assert(sz == 0 || fi < vf.size());
        sb.write(reinterpret_cast<const char*>(fragbuf.data()), sz);
        ++frags;
        redundancy += hits;
        exe += exe1 * 4;
        text += text1 * 2;
        if (sz >= MIN_FRAGMENT) {
          std::memmove(o1prev, o1prev + 256, 256 * (ON - 1));
          std::memcpy(o1prev + 256 * (ON - 1), o1, 256);
        }
      }

      // Update HT and ptr list.
      if (fi < vf.size()) {
        if (htptr == 0) {
          htptr = unsigned(ht.size());
          ht.push_back(HT(sha1result, int(sz)));
          htinv.update();
          fsize += sz;
        }
        vf[fi]->second.ptr.push_back(htptr);
      }
      if (c == -1) break;
    }  // end for each fragment fj
    if (fi < vf.size()) {
      dedupesize += fsize;
      DTMap::iterator p = vf[fi];
      std::string newname = rename(p->first.c_str());
      DTMap::iterator a = dt.find(newname);
      if (a == dt.end() || a->second.date == 0) printf("+ ");
      else printf("# ");
      printf("%s", p->first.c_str());
      if (newname != p->first) {
        printf(" -> ");
        printf("%s", newname.c_str());
      }
      printf(" %1.0f", p->second.size + 0.0);
      if (fsize != p->second.size) printf(" -> %1.0f", fsize + 0.0);
      printf("\n");
    }
  }  // end for each file fi
  assert(sb.size() == 0);

  // Append compressed fragment tables to archive.
  std::int64_t cdatasize = out.tell() - header_end;
  io::Buffer is;
  assert(blocklist.size() == csize_list_.size());
  blocklist.push_back(unsigned(ht.size()));
  for (unsigned i = 0; i < csize_list_.size(); ++i) {
    if (blocklist[i] < blocklist[i + 1]) {
      puti(is, csize_list_[i], 4);  // compressed size of block
      for (unsigned j = blocklist[i]; j < blocklist[i + 1]; ++j) {
        is.write(reinterpret_cast<const char*>(ht[j].sha1), 20);
        puti(is, ht[j].usize, 4);
      }
      compression::compress_block(&is, &out, "0",
                                  ("jDC" + itoa64(date, 14) + "h" + itoa64(blocklist[i], 10)).c_str(),
                                  "jDC\x01", true);
      is.reset();
    }
  }

  // Delete from archive.
  int dtcount = 0;
  int removed = 0;
  for (DTMap::iterator p = dt.begin(); p != dt.end(); ++p) {
    if (p->second.date && !p->second.data) {
      puti(is, 0, 8);
      is.write(p->first.c_str(), p->first.size());
      is.put(0);
      printf("- %s\n", p->first.c_str());
      ++removed;
      if (is.size() > 16000) {
        compression::compress_block(&is, &out, "1",
                                    ("jDC" + itoa64(date) + "i" + itoa64(++dtcount, 10)).c_str(),
                                    "jDC\x01", true);
        is.reset();
      }
    }
  }

  // Append compressed index to archive.
  int added = 0;
  for (DTMap::iterator p = edt.begin();; ++p) {
    if (p != edt.end()) {
      std::string filename = rename(p->first);
      DTMap::iterator a = dt.find(filename);
      if (p->second.date && (a == dt.end() || a->second.date != p->second.date ||
                             (a->second.attr && a->second.attr != p->second.attr) ||
                             a->second.size != p->second.size ||
                             (p->second.data && a->second.ptr != p->second.ptr))) {
        ++added;
        puti(is, p->second.date, 8);
        is.write(filename.c_str(), filename.size());
        is.put(0);
        if ((p->second.attr & 255) == 'u') {  // unix attributes
          puti(is, 3, 4);
          puti(is, p->second.attr, 3);
        } else if ((p->second.attr & 255) == 'w') {  // windows attributes
          puti(is, 5, 4);
          puti(is, p->second.attr, 5);
        } else
          puti(is, 0, 4);  // no attributes
        if (a == dt.end() || p->second.data) a = p;
        puti(is, a->second.ptr.size(), 4);  // list of frag pointers
        for (unsigned i = 0; i < a->second.ptr.size(); ++i) puti(is, a->second.ptr[i], 4);
      }
    }
    if (is.size() > 16000 || (is.size() > 0 && p == edt.end())) {
      compression::compress_block(&is, &out, "1",
                                  ("jDC" + itoa64(date) + "i" + itoa64(++dtcount, 10)).c_str(),
                                  "jDC\x01", true);
      is.reset();
    }
    if (p == edt.end()) break;
  }
  printf("%d +added, %d -removed.\n", added, removed);
  assert(is.size() == 0);

  // Back up and write the header.
  std::int64_t archive_end = out.tell();
  out.seek(header_pos);
  writeJidacHeader(&out, date, cdatasize, htsize);

  // Truncate empty update from archive.
  if (added + removed == 0 && archive_end - header_pos == 104) archive_end = header_pos;
  if (archive_end < out.size()) {
    printf("truncating archive from %1.0f to %1.0f\n", double(out.size()),
           double(archive_end));
    out.flush();
    out.truncate(archive_end);
  }
  out.flush();
  fprintf(stderr, "\n%1.6f + (%1.6f -> %1.6f -> %1.6f) = %1.6f MB\n",
          header_pos / 1000000.0, total_size / 1000000.0, dedupesize / 1000000.0,
          (archive_end - header_pos) / 1000000.0, archive_end / 1000000.0);
  return errors > 0;
}

// ------------------------------- extract --------------------------------

// Decompress a block and write its fragments to the files in dt that point
// to it. Single-threaded port of decompressThread.
void extract_block(Jidac& jd, Block& b, const std::string& arc,
                   io::Buffer& out) {
  // Get uncompressed size of block.
  unsigned output_size = 0;
  assert(b.start > 0);
  for (unsigned j = 0; j < b.size; ++j) {
    assert(b.start + j < jd.ht.size());
    assert(jd.ht[b.start + j].usize >= 0);
    output_size += jd.ht[b.start + j].usize;
  }

  FileIn in(arc.c_str());
  if (!in.is_open()) throw std::runtime_error("archive not found");
  in.seek(b.offset);
  decompression::BlockDecoder d;
  d.set_input(&in);
  out.reset();
  d.set_output(&out);
  if (!d.find_block()) throw format_error("archive block not found");
  bool got = false;
  while (d.find_filename()) {
    d.read_comment();
    while (out.size() < output_size && d.decompress(1 << 14))
      ;
    if (out.size() >= output_size) {
      got = true;
      break;
    }
    d.read_segment_end();
  }
  if (!got || out.size() < output_size) {
    fprintf(stderr, "output [%u..%u] %zu of %u bytes\n", b.start, b.start + b.size - 1,
            out.size(), output_size);
    throw format_error("unexpected end of compressed data");
  }

  // Verify fragment checksums if present.
  std::uint64_t q = 0;  // fragment start
  integrity::SHA1 sha1;
  for (unsigned j = b.start; j < b.start + b.size; ++j) {
    assert(j > 0 && j < jd.ht.size());
    assert(jd.ht[j].usize >= 0);
    if (q + jd.ht[j].usize > out.size()) throw format_error("Incomplete decompression");
    char sha1result[20];
    sha1.write(reinterpret_cast<const char*>(out.data()) + q, jd.ht[j].usize);
    std::memcpy(sha1result, sha1.result(), 20);
    q += jd.ht[j].usize;
    if (std::memcmp(sha1result, jd.ht[j].sha1, 20)) {
      fprintf(stderr, "fragment %u size %d checksum failed\n", j, jd.ht[j].usize);
      throw format_error("bad checksum");
    }
    ++b.extracted;
  }

  // Write the files in dt that point to this block.
  DTMap::iterator lastdt = jd.dt.end();  // last file written
  FILE* outf = nullptr;                  // output file
  for (unsigned ip = 0; ip < b.files.size(); ++ip) {
    DTMap::iterator p = b.files[ip];
    if (p->second.date == 0 || p->second.data < 0 ||
        p->second.data >= std::int64_t(p->second.ptr.size()))
      continue;  // don't write

    const std::vector<unsigned>& ptr = p->second.ptr;
    std::int64_t offset = 0;
    for (unsigned j = 0; j < ptr.size(); ++j) {
      if (ptr[j] < b.start || ptr[j] >= b.start + b.extracted) {
        offset += jd.ht[ptr[j]].usize;
        continue;
      }

      // Open file for output (new file, or update existing file).
      if (p != lastdt) {  // new file
        if (outf) std::fclose(outf);
        outf = nullptr;
        lastdt = jd.dt.end();
      }
      if (lastdt == jd.dt.end()) {  // no file open
        const std::string filename = jd.rename(p->first);
        if (!jd.dotest) {
          if (p->second.data == 0) {  // first fragment
            makepath(filename);
            outf = open_file(filename.c_str(), "wb");
            if (!outf) fprintf(stderr, "cannot write %s\n", filename.c_str());
          } else {  // update existing file
            outf = open_file(filename.c_str(), "r+b");
            if (!outf) fprintf(stderr, "cannot update %s\n", filename.c_str());
          }
        }
        if (!jd.dotest && !outf) break;
        lastdt = p;
      }

      // Find block offset of fragment.
      std::uint64_t qq = 0;
      for (unsigned k = b.start; k < ptr[j]; ++k) {
        assert(k > 0 && k < jd.ht.size());
        if (jd.ht[k].usize < 0) throw format_error("streaming fragment in file");
        qq += jd.ht[k].usize;
      }
      assert(qq + jd.ht[ptr[j]].usize <= out.size());

      // Combine consecutive fragments into a single write.
      ++p->second.data;
      std::uint64_t usize = jd.ht[ptr[j]].usize;
      while (j + 1 < ptr.size() && ptr[j + 1] == ptr[j] + 1 &&
             ptr[j + 1] < b.start + b.size && jd.ht[ptr[j + 1]].usize >= 0 &&
             usize + jd.ht[ptr[j + 1]].usize <= 0x7fffffff) {
        ++p->second.data;
        usize += jd.ht[ptr[++j]].usize;
      }

      // Write the merged fragment unless they are all zeros and it
      // does not include the last fragment.
      std::uint64_t nz = qq;  // first nonzero byte in fragments to be written
      while (nz < qq + usize && out.data()[nz] == 0) ++nz;
      if (!jd.dotest && (nz < qq + usize || j + 1 == ptr.size())) {
        std::fseek(outf, long(offset), SEEK_SET);
        std::fwrite(reinterpret_cast<const char*>(out.data()) + qq, 1, usize, outf);
      }
      offset += usize;

      // Close file. If this is the last fragment then set date and attr.
      // Do not set read-only attribute in Windows yet.
      if (p->second.data == std::int64_t(ptr.size())) {
        assert(p->second.date);
        if (!jd.dotest) {
          assert(outf);
          const std::string fn = jd.rename(p->first);
          std::int64_t attr = p->second.attr;
          std::int64_t date = p->second.date;
          if ((p->second.attr & 0x1ff) == 'w' + 256) attr = 0;  // read-only?
          std::fclose(outf);
          outf = nullptr;
          jd.close_file(fn.c_str(), date, attr);
        }
        lastdt = jd.dt.end();
      }
    }  // end for j
  }    // end for ip
}

int Jidac::extract() {
  const std::int64_t sz = read_archive(archive.c_str());
  if (sz < 1) throw std::runtime_error("archive not found");

  // Test blocks.
  for (unsigned i = 0; i < block.size(); ++i) {
    if (block[i].bsize < 0) throw format_error("negative block size");
    if (block[i].start < 1) throw format_error("block starts at fragment 0");
    if (block[i].start >= ht.size()) throw format_error("block start too high");
    if (i > 0 && block[i].start < block[i - 1].start) throw format_error("unordered frags");
    if (i > 0 && block[i].start == block[i - 1].start) throw format_error("empty block");
    if (i > 0 && block[i].offset < block[i - 1].offset + block[i - 1].bsize)
      throw format_error("unordered blocks");
    if (i > 0 && block[i - 1].offset + block[i - 1].bsize > block[i].offset)
      throw format_error("overlapping blocks");
  }

  // Label files to extract with data=0.
  // Skip existing output files. If force then skip only if equal
  // and set date and attributes.
  int total_files = 0, skipped = 0;
  std::int64_t total_size = 0;
  for (DTMap::iterator p = dt.begin(); p != dt.end(); ++p) {
    p->second.data = -1;  // skip
    if (p->second.date && p->first != "") {
      const std::string fn = rename(p->first);
      const bool isdir = p->first[p->first.size() - 1] == '/';
      if (!dotest && force && !isdir && equal(p, fn.c_str())) {
        if (summary <= 0) {
          printf("= %s\n", fn.c_str());
        }
        close_file(fn.c_str(), p->second.date, p->second.attr);
        ++skipped;
      } else if (!dotest && !force && exists(fn)) {  // exists, skip
        if (summary <= 0) {
          printf("? %s\n", fn.c_str());
        }
        ++skipped;
      } else if (isdir)  // update directories later
        p->second.data = 0;
      else if (block.size() > 0) {  // files to decompress
        p->second.data = 0;
        unsigned lo = 0, hi = unsigned(block.size() - 1);  // block indexes
        for (unsigned i = 0; p->second.data >= 0 && i < p->second.ptr.size(); ++i) {
          const unsigned j = p->second.ptr[i];  // fragment index
          if (j == 0 || j >= ht.size() || ht[j].usize < -1) {
            fflush(stdout);
            fprintf(stderr, "%s: bad frag IDs, skipping...\n", p->first.c_str());
            p->second.data = -1;  // skip
            continue;
          }
          assert(j > 0 && j < ht.size());
          if (lo != hi || lo >= block.size() || j < block[lo].start ||
              (lo + 1 < block.size() && j >= block[lo + 1].start)) {
            lo = 0;  // find block with fragment j by binary search
            hi = unsigned(block.size() - 1);
            while (lo < hi) {
              const unsigned mid = (lo + hi + 1) / 2;
              assert(mid > lo);
              assert(mid <= hi);
              if (j < block[mid].start) hi = mid - 1;
              else lo = mid;
            }
          }
          assert(lo == hi);
          assert(lo < block.size());
          assert(j >= block[lo].start);
          assert(lo + 1 == block.size() || j < block[lo + 1].start);
          const unsigned c = j - block[lo].start + 1;
          if (block[lo].size < c) block[lo].size = c;
          if (block[lo].files.size() == 0 || block[lo].files.back() != p)
            block[lo].files.push_back(p);
        }
        ++total_files;
        total_size += p->second.size;
      }
    }
  }
  if (!force && skipped > 0)
    printf("%d ?existing files skipped (-force overwrites).\n", skipped);
  if (force && skipped > 0)
    printf("%d =identical files skipped.\n", skipped);

  // Decompress blocks and write files (single-threaded).
  printf("Extracting %1.6f MB in %d files\n", total_size / 1000000.0, total_files);
  io::Buffer out;
  for (unsigned i = 0; i < block.size(); ++i) {
    if (block[i].size > 0 && block[i].usize >= 0) {
      try {
        extract_block(*this, block[i], archive, out);
      } catch (std::exception& e) {
        fprintf(stderr, "skipping [%u..%u] at %1.0f: %s\n", block[i].start,
                block[i].start + block[i].size - 1, block[i].offset + 0.0, e.what());
      }
    }
  }

  // Extract streaming files.
  unsigned segments = 0;  // count
  FileIn in(archive.c_str());
  if (in.is_open()) {
    FILE* outf = nullptr;
    DTMap::iterator dtptr = dt.end();
    for (unsigned i = 0; i < block.size(); ++i) {
      if (block[i].usize < 0 && block[i].size > 0) {
        Block& b = block[i];
        try {
          in.seek(b.offset);
          decompression::BlockDecoder d;
          d.set_input(&in);
          if (!d.find_block()) throw format_error("block not found");
          io::MemoryWriter filename;
          for (unsigned j = 0; j < b.size; ++j) {
            filename.clear();
            if (!d.find_filename(&filename)) throw format_error("segment not found");
            d.read_comment();

            // Start of new output file
            if (filename.bytes().size() != 0 || segments == 0) {
              unsigned k;
              for (k = 0; k < b.files.size(); ++k) {  // find in dt
                if (b.files[k]->second.ptr.size() > 0 &&
                    b.files[k]->second.ptr[0] == b.start + j &&
                    b.files[k]->second.date > 0 && b.files[k]->second.data == 0)
                  break;
              }
              if (k < b.files.size()) {  // found new file
                if (outf) std::fclose(outf);
                outf = nullptr;
                const std::string outname = rename(b.files[k]->first);
                dtptr = b.files[k];
                if (summary <= 0) {
                  printf("> %s\n", outname.c_str());
                }
                if (!dotest) {
                  makepath(outname);
                  outf = open_file(outname.c_str(), "wb");
                  if (!outf) fprintf(stderr, "cannot write %s\n", outname.c_str());
                }
              } else {  // end of file
                if (outf) std::fclose(outf);
                outf = nullptr;
                dtptr = dt.end();
              }
            }

            // Decompress segment
            integrity::SHA1 sha1;
            d.set_sha1(&sha1);
            io::Buffer os;
            d.set_output(&os);
            d.decompress(1 << 30);

            // Verify checksum
            char sha1result[21];
            d.read_segment_end(sha1result);
            if (sha1result[0] == 1) {
              if (std::memcmp(sha1result + 1, sha1.result(), 20) != 0)
                throw format_error("bad checksum");
            }

            // Write the segment
            if (!dotest && outf) {
              std::fwrite(reinterpret_cast<const char*>(os.data()), 1, os.size(), outf);
            }
            ++segments;
          }  // end for j
        } catch (std::exception& e) {
          fprintf(stderr, "skipping [%u..%u] at %1.0f: %s\n", b.start,
                  b.start + b.size - 1, b.offset + 0.0, e.what());
        }
      }  // end if usize < 0
    }    // end for i
    if (outf) std::fclose(outf);
  }  // end if in open
  return 0;
}

// -------------------------------- list ---------------------------------

void Jidac::dump_state() const {
  printf("-- blocks --\n");
  for (unsigned i = 0; i < block.size(); ++i) {
    printf("block[%u] start=%lld size=%lld usize=%lld bsize=%lld offset=%lld frags=%u\n",
           i, (long long)block[i].start, (long long)block[i].size,
           (long long)block[i].usize, (long long)block[i].bsize,
           (long long)block[i].offset, block[i].frags);
  }
  printf("-- fragments (first 8) --\n");
  for (unsigned i = 1; i < ht.size() && i < 9; ++i)
    printf("  ht[%u] usize=%d\n", i, ht[i].usize);
  printf("-- files --\n");
  for (DTMap::const_iterator p = dt.begin(); p != dt.end(); ++p) {
    printf("  %s date=%lld size=%lld data=%lld ptr=[",
           p->first.c_str(), (long long)p->second.date, (long long)p->second.size,
           (long long)p->second.data);
    for (unsigned j = 0; j < p->second.ptr.size(); ++j)
      printf("%s%u", j ? "," : "", p->second.ptr[j]);
    printf("]\n");
  }
}

int Jidac::list() {
  read_archive(archive.c_str());
  for (DTMap::iterator p = dt.begin(); p != dt.end(); ++p) {
    if (p->second.date == 0) continue;
    printf("%s %1.0f\n", p->first.c_str(), p->second.size + 0.0);
  }
  return 0;
}

}  // namespace zpaq_ng::archive