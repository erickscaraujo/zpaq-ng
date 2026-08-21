// zpaq_ng_demo.cpp - Command line demo for the ZPAQ-NG compression core.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Exercises the block encoder/decoder and postprocessor end to end:
//
//   zpaq_ng_demo c <input> <output.zpaq> [level 0-3]   compress
//   zpaq_ng_demo d <input.zpaq> <output>               decompress first segment
//   zpaq_ng_demo t <input>                             roundtrip self-test
//
// The produced .zpaq is a single-block, single-segment archive decodable by
// the original zpaq -m0..3 equivalent blocks.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "compression/block_encoder.hpp"
#include "decompression/block_decoder.hpp"
#include "integrity/sha1.hpp"
#include "io/streams.hpp"

using namespace zpaq_ng;

namespace {

// File backed Reader.
class FileReader final : public io::Reader {
public:
  explicit FileReader(const std::string& path) {
    in_.open(path, std::ios::binary);
    if (!in_) throw io_error("cannot open " + path + " for reading");
  }
  int get() override { return in_.get(); }
  std::size_t read(char* buf, std::size_t n) override {
    in_.read(buf, static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(in_.gcount());
  }
private:
  std::ifstream in_;
};

// File backed Writer.
class FileWriter final : public io::Writer {
public:
  explicit FileWriter(const std::string& path) {
    out_.open(path, std::ios::binary);
    if (!out_) throw io_error("cannot open " + path + " for writing");
  }
  void put(int c) override { out_.put(static_cast<char>(c)); }
  void write(const char* buf, std::size_t n) override {
    out_.write(buf, static_cast<std::streamsize>(n));
  }
  void close() { if (out_) out_.close(); }
private:
  std::ofstream out_;
};

// Read a whole file into a string.
std::string read_all(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw io_error("cannot open " + path + " for reading");
  return std::string(std::istreambuf_iterator<char>(f), {});
}

// Write a whole string to a file.
void write_all(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary);
  if (!f) throw io_error("cannot open " + path + " for writing");
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f) throw io_error("failed writing " + path);
}

int cmd_compress(const std::string& in_path, const std::string& out_path,
                 int level) {
  FileReader in(in_path);
  FileWriter out(out_path);
  compression::BlockEncoder enc;
  enc.set_output(&out);
  enc.set_verify(true);
  enc.write_tag();
  if (level == 0) enc.start_block_store();
  else if (level >= 1 && level <= 3) enc.start_block(level);
  else throw invalid_argument_error("level must be 0..3");
  enc.start_segment(in_path.substr(in_path.find_last_of("/\\") + 1),
                    "created by zpaq_ng_demo");
  enc.set_input(&in);
  enc.compress(-1);
  const unsigned char* sha = enc.end_segment_checksum(true);
  enc.end_block();
  out.close();
  std::fprintf(stderr, "compressed %s -> %s (level %d, sha1 %s)\n",
               in_path.c_str(), out_path.c_str(), level,
               sha ? "ok" : "not computed");
  return 0;
}

int cmd_decompress(const std::string& in_path, const std::string& out_path) {
  FileReader in(in_path);
  decompression::BlockDecoder dec;
  dec.set_input(&in);
  FileWriter out(out_path);
  dec.set_output(&out);
  integrity::SHA1 sha1;
  dec.set_sha1(&sha1);
  if (!dec.find_block()) throw format_error("no ZPAQ block found");
  if (!dec.find_filename()) throw format_error("no segment found");
  dec.read_comment();
  while (dec.decompress()) {}
  char sha1str[21] = {0};
  dec.read_segment_end(sha1str);
  out.close();
  std::fprintf(stderr, "decompressed %s -> %s (sha1 %s)\n", in_path.c_str(),
               out_path.c_str(), sha1str[0] ? "verified" : "unchecked");
  return 0;
}

int cmd_roundtrip(const std::string& path) {
  const std::string data = read_all(path);
  for (int level = 0; level <= 3; ++level) {
    io::MemoryWriter out;
    compression::BlockEncoder enc;
    enc.set_output(&out);
    enc.set_verify(true);
    enc.write_tag();
    if (level == 0) enc.start_block_store();
    else enc.start_block(level);
    enc.start_segment("test", "");
    io::MemoryReader in(data);
    enc.set_input(&in);
    enc.compress(-1);
    const unsigned char* encsha = enc.end_segment_checksum(true);
    enc.end_block();
    if (!encsha) { std::printf("  level %d: no checksum\n", level); return 1; }

    const std::vector<byte>& ob = out.bytes();
    io::MemoryReader r(ob.data(), ob.size());
    decompression::BlockDecoder dec;
    dec.set_input(&r);
    io::MemoryWriter out2;
    dec.set_output(&out2);
    integrity::SHA1 sha1;
    dec.set_sha1(&sha1);
    if (!dec.find_block() || !dec.find_filename()) {
      std::printf("  level %d: header parse failed\n", level);
      return 1;
    }
    dec.read_comment();
    while (dec.decompress()) {}
    char sha1str[21] = {0};
    dec.read_segment_end(sha1str);
    const std::string got(reinterpret_cast<const char*>(out2.bytes().data()),
                          out2.size());
    if (got != data) {
      std::printf("  level %d: DATA MISMATCH (%zu vs %zu)\n", level, got.size(),
                  data.size());
      return 1;
    }
    if (!sha1str[0]) { std::printf("  level %d: missing checksum\n", level); return 1; }
    const char* decsha = sha1.result();
    for (int i = 0; i < 20; ++i)
      if (static_cast<unsigned char>(decsha[i]) != encsha[i]) {
        std::printf("  level %d: SHA1 MISMATCH\n", level);
        return 1;
      }
    std::printf("  level %d: OK (%zu bytes -> %zu bytes)\n", level, data.size(),
                out.size());
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) {
      std::fprintf(stderr,
                   "usage: zpaq_ng_demo c <input> <output.zpaq> [level 0-3]\n"
                   "       zpaq_ng_demo d <input.zpaq> <output>\n"
                   "       zpaq_ng_demo t <input>\n");
      return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "c")
      return cmd_compress(argv[2], argv[3], argc > 4 ? std::atoi(argv[4]) : 1);
    if (cmd == "d") return cmd_decompress(argv[2], argv[3]);
    if (cmd == "t") return cmd_roundtrip(argv[2]);
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}