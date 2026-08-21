// jidac.hpp - JIDAC journaling archive layer.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Faithful single-threaded port of the JIDAC archive format from zpaq.cpp:
// the transaction (c), fragment data (d), fragment table (h), and index (i)
// blocks, with the "jDC<date>[cdhi]<num>" filenames and " jDC\x01" comments.
// Archives written here are byte-identical to those of the original zpaq and
// vice versa. Fragment data is deduplicated by SHA1 across the whole archive.

#ifndef ZPAQ_NG_ARCHIVE_JIDAC_HPP
#define ZPAQ_NG_ARCHIVE_JIDAC_HPP

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "io/streams.hpp"

namespace zpaq_ng::archive {

// A seekable Reader over a std::FILE (the archive).
class FileIn final : public io::Reader {
public:
  explicit FileIn(const char* name);
  FileIn(FileIn&& o) noexcept : f_(o.f_), pos_(o.pos_) { o.f_ = nullptr; }
  FileIn& operator=(FileIn&& o) noexcept {
    if (this != &o) {
      close();
      f_ = o.f_;
      pos_ = o.pos_;
      o.f_ = nullptr;
    }
    return *this;
  }
  FileIn(const FileIn&) = delete;
  FileIn& operator=(const FileIn&) = delete;
  ~FileIn() { close(); }
  int get() override;
  std::size_t read(char* buf, std::size_t n) override;
  void seek(std::int64_t pos);
  std::int64_t tell() const;
  bool is_open() const { return f_ != nullptr; }
  void close();

private:
  std::FILE* f_ = nullptr;
  std::int64_t pos_ = 0;
};

// A Writer over a std::FILE (the archive).
class FileOut final : public io::Writer {
public:
  explicit FileOut(const char* name);
  FileOut(FileOut&& o) noexcept : f_(o.f_), pos_(o.pos_) { o.f_ = nullptr; }
  FileOut& operator=(FileOut&& o) noexcept {
    if (this != &o) {
      close();
      f_ = o.f_;
      pos_ = o.pos_;
      o.f_ = nullptr;
    }
    return *this;
  }
  FileOut(const FileOut&) = delete;
  FileOut& operator=(const FileOut&) = delete;
  ~FileOut() { close(); }
  void put(int c) override;
  void write(const char* buf, std::size_t n) override;
  void seek(std::int64_t pos);
  void seek_end();
  std::int64_t tell() const;
  std::int64_t size() const;
  bool is_open() const { return f_ != nullptr; }
  void truncate(std::int64_t size);
  void flush();
  void close();

private:
  std::FILE* f_ = nullptr;
  std::int64_t pos_ = 0;
};

// Fragment hash table entry.
struct HT {
  unsigned char sha1[20];  // fragment hash
  int usize;               // uncompressed size, -1 if unknown, -2 if not init
  HT(const char* s = 0, int u = -2) {
    if (s)
      std::memcpy(sha1, s, 20);
    else
      std::memset(sha1, 0, 20);
    usize = u;
  }
};

// Filename entry.
struct DT {
  std::int64_t date;         // decimal YYYYMMDDHHMMSS (UT) or 0 if deleted
  std::int64_t size;         // size or -1 if unknown
  std::int64_t attr;         // first 8 attribute bytes
  std::int64_t data;         // sort key or frags written. -1 = do not write
  std::vector<unsigned> ptr; // fragment list
  DT() : date(0), size(0), attr(0), data(0) {}
};
using DTMap = std::map<std::string, DT>;

// List of blocks to extract.
struct Block {
  std::int64_t offset;    // location in archive
  std::int64_t usize;     // uncompressed size, -1 if unknown (streaming)
  std::int64_t bsize;     // compressed size
  std::vector<DTMap::iterator> files;  // list of files pointing here
  unsigned start;         // index in ht of first fragment
  unsigned size;          // number of fragments to decompress
  unsigned frags;         // number of fragments in block
  unsigned extracted;     // number of fragments decompressed OK
  enum { READY, WORKING, GOOD, BAD } state;
  Block(unsigned s, std::int64_t o)
      : offset(o), usize(-1), bsize(0), start(s), size(0), frags(0),
        extracted(0), state(READY) {}
};

// Version info.
struct VER {
  std::int64_t date;         // Date of C block, 0 if streaming
  std::int64_t lastdate;     // Latest date of any block
  std::int64_t offset;       // start of transaction C block
  std::int64_t data_offset;  // start of first D block
  std::int64_t csize;        // size of compressed data, -1 = no index
  int updates;               // file updates
  int deletes;               // file deletions
  unsigned firstFragment;    // first fragment ID
  VER() : date(0), lastdate(0), offset(0), data_offset(0), csize(0),
          updates(0), deletes(0), firstFragment(0) {}
};

// Utility functions shared with the CLI.
std::string itoa64(std::int64_t x, int n = 1);  // decimal, at least n digits
std::int64_t decimal_time(std::int64_t unix);   // YYYYMMDDHHMMSS (UT)
std::int64_t unix_time(std::int64_t dec);       // inverse of decimal_time
bool ispath(const char* a, const char* b);      // glob match with * and ?
std::string append_path(std::string a, std::string b);

// A Jidac object represents an archive: a list of file fragments with hash,
// size, and archive offset, and a list of files with date, attributes, and
// fragment pointers.
class Jidac {
public:
  // Command line options
  char command = 0;                 // 'a', 'x', or 'l'
  std::string archive;              // archive name
  std::vector<std::string> files;   // filename args
  int all = 0;                      // -all option
  bool force = false;               // -force option
  int fragment = 6;                 // -fragment option
  std::string method;               // default "1"
  bool noattributes = false;        // -noattributes option
  std::vector<std::string> notfiles;// list of prefixes to exclude
  std::vector<std::string> onlyfiles;// list of prefixes to include
  int summary = 0;                  // -summary (progress)
  bool dotest = false;              // -test option
  std::vector<std::string> tofiles; // -to option
  std::int64_t date = 0;            // now as decimal YYYYMMDDHHMMSS (UT)
  std::int64_t version = 99999999999999LL;  // version number or 14 digit date

  Jidac() { ver.resize(1); }  // version 0

  // Commands
  int add();     // add, return 1 if error else 0
  int extract(); // extract, return 1 if error else 0
  int list();    // list, return 0

  // Read the archive or index into ht, dt, ver. Return size in bytes.
  std::int64_t read_archive(const char* arc, int* errors = nullptr);

  // Number of versions read (for -until -N).
  std::size_t version_count() const { return ver.size(); }

  // Number of files in the index.
  std::size_t dt_count() const { return dt.size(); }

  // Debug helpers
  std::size_t block_count() const { return block.size(); }
  std::size_t fragment_count() const { return ht.size(); }
  void dump_state() const;

private:
  // Archive state
  std::int64_t dhsize = 0;    // total size of D blocks according to H blocks
  std::int64_t dcsize = 0;    // total size of D blocks according to C blocks
  std::vector<HT> ht;         // list of fragments (index 0 unused)
  DTMap dt;                   // set of files in archive
  DTMap edt;                  // set of external files to add or compare
  std::vector<Block> block;   // list of data blocks to extract
  std::vector<VER> ver;       // version info
  std::vector<std::int64_t> csize_list_;  // compressed sizes of D blocks

  bool isselected(const char* filename, bool rn = false);
  void scandir(const std::string& filename);
  void addfile(std::string filename, std::int64_t edate, std::int64_t esize,
               std::int64_t eattr);
  std::string rename(const std::string& name);
  bool equal(DTMap::const_iterator p, const char* filename);
  void close_file(const char* filename, std::int64_t date, std::int64_t attr);

  friend void extract_block(Jidac& jd, Block& b, const std::string& arc,
                            io::Buffer& out);
};

} // namespace zpaq_ng::archive

#endif // ZPAQ_NG_ARCHIVE_JIDAC_HPP