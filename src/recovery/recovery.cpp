// recovery.cpp - Recovery implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "recovery/recovery.hpp"

#include <cstring>

#include "core/error.hpp"
#include "decompression/block_decoder.hpp"
#include "integrity/sha1.hpp"

namespace zpaq_ng::recovery {

namespace {

// A seekable in-memory Reader over a sub-range [begin, end) of a buffer.
class SliceReader final : public io::Reader {
public:
  SliceReader(const byte* data, std::size_t begin, std::size_t end)
      : data_(data), begin_(begin), end_(end), pos_(begin) {}
  int get() override { return pos_ < end_ ? data_[pos_++] : -1; }
  std::size_t read(char* buf, std::size_t n) override {
    n = std::min<std::size_t>(n, 64);
    const std::size_t take = std::min(n, end_ - pos_);
    std::memcpy(buf, data_ + pos_, take);
    pos_ += take;
    return take;
  }
  std::size_t tell() const noexcept { return pos_; }
  std::size_t size() const noexcept { return end_; }

private:
  const byte* data_;
  std::size_t begin_, end_, pos_;
};

// Collect every "zPQ" block tag position. A few spurious matches inside
// stored data are possible; each candidate is validated by decoding it.
std::vector<std::size_t> find_tags(ConstBytes archive) {
  std::vector<std::size_t> tags;
  for (std::size_t i = 0; i + 3 <= archive.size(); ++i) {
    if (archive[i] == 'z' && archive[i + 1] == 'P' && archive[i + 2] == 'Q')
      tags.push_back(i);
  }
  return tags;
}

} // namespace

RecoveryReport recover_stream(ConstBytes archive, io::Writer* out) {
  RecoveryReport rep;
  const auto tags = find_tags(archive);
  bool after_resync = false;

  // Each block is decoded from its own isolated slice [tag, next_tag); this
  // avoids relying on the decoder's read-ahead byte accounting, which would
  // otherwise skip block boundaries.
  for (std::size_t i = 0; i < tags.size(); ++i) {
    const std::size_t slice_end =
        i + 1 < tags.size() ? tags[i + 1] : archive.size();
    SliceReader sr(archive.data(), tags[i], slice_end);

    decompression::BlockDecoder dec;
    dec.set_input(&sr);
    io::MemoryWriter block_out;  // only flushed when the whole block is valid
    dec.set_output(&block_out);
    dec.set_sha1(nullptr);  // set per segment below

    bool ok = false;
    try {
      if (!dec.find_block()) continue;
      while (dec.find_filename()) {
        dec.read_comment();
        integrity::SHA1 sha1;
        dec.set_sha1(&sha1);
        while (dec.decompress(-1)) {
        }
        char sha1result[21] = {0};
        dec.read_segment_end(sha1result);
        if (sha1result[0] && std::memcmp(sha1result + 1, sha1.result(), 20))
          throw format_error("bad checksum");
      }
      ok = true;
    } catch (const std::exception&) {
      ok = false;
    }

    BlockStatus bs;
    bs.index = rep.blocks.size();
    if (ok) {
      // Only fully verified blocks are written out.
      const auto& bytes = block_out.bytes();
      bs.output_bytes = bytes.size();
      rep.output_bytes += bytes.size();
      out->write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (after_resync) {
        bs.status = "recovered";
        ++rep.recovered;
      } else {
        bs.status = "ok";
        ++rep.ok;
      }
      after_resync = false;
    } else {
      bs.status = "corrupted";
      ++rep.corrupted;
      after_resync = true;
    }
    rep.blocks.push_back(std::move(bs));
  }
  return rep;
}

std::string report(const RecoveryReport& r) {
  std::string s;
  s += "Recovery report\n";
  s += "---------------\n";
  for (const auto& b : r.blocks) {
    s += "  Block " + std::to_string(b.index) + ": " + b.status + " (" +
         std::to_string(b.output_bytes) + " bytes)\n";
  }
  s += std::to_string(r.ok) + " blocks OK, " +
       std::to_string(r.corrupted) + " corrupted, " +
       std::to_string(r.recovered) + " recovered.\n";
  return s;
}

} // namespace zpaq_ng::recovery