// streams.hpp - Byte stream abstraction for ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The compression core is written against two abstract byte streams, Reader
// and Writer, exactly as in the original libzpaq. Concrete implementations
// adapt files, memory buffers, and sockets. All numeric code paths only depend
// on these two interfaces, which keeps the core testable in isolation.

#ifndef ZPAQ_NG_IO_STREAMS_HPP
#define ZPAQ_NG_IO_STREAMS_HPP

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/types.hpp"

namespace zpaq_ng::io {

// A byte source.
class Reader {
public:
  virtual ~Reader() = default;

  // Return the next byte 0..255, or -1 at EOF.
  virtual int get() = 0;

  // Read up to n bytes into buf. Returns the number actually read.
  virtual std::size_t read(char* buf, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
      const int c = get();
      if (c < 0) break;
      buf[total++] = static_cast<char>(c);
    }
    return total;
  }

  // Skip n bytes; returns the number skipped.
  virtual std::size_t skip(std::size_t n) {
    char scratch[4096];
    std::size_t total = 0;
    while (total < n) {
      const std::size_t chunk = std::min<std::size_t>(n - total, sizeof scratch);
      const std::size_t r = read(scratch, chunk);
      total += r;
      if (r == 0) break;
    }
    return total;
  }
};

// A byte sink.
class Writer {
public:
  virtual ~Writer() = default;

  // Write the low 8 bits of c.
  virtual void put(int c) = 0;

  // Write buf[0..n-1].
  virtual void write(const char* buf, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) put(static_cast<unsigned char>(buf[i]));
  }
};

// A Reader over a contiguous memory range.
class MemoryReader final : public Reader {
public:
  MemoryReader(const byte* p, std::size_t n) : data_(p), size_(n), pos_(0) {}
  explicit MemoryReader(std::string_view s)
      : MemoryReader(reinterpret_cast<const byte*>(s.data()), s.size()) {}

  int get() override {
    return pos_ < size_ ? data_[pos_++] : -1;
  }

  std::size_t read(char* buf, std::size_t n) override {
    const std::size_t avail = size_ - pos_;
    const std::size_t take = std::min(n, avail);
    std::memcpy(buf, data_ + pos_, take);
    pos_ += take;
    return take;
  }

  std::size_t position() const noexcept { return pos_; }
  std::size_t size() const noexcept { return size_; }

private:
  const byte* data_;
  std::size_t size_;
  std::size_t pos_;
};

// A Writer that appends to an owned byte vector.
class MemoryWriter final : public Writer {
public:
  void put(int c) override {
    buffer_.push_back(static_cast<byte>(c));
  }
  void write(const char* buf, std::size_t n) override {
    const byte* p = reinterpret_cast<const byte*>(buf);
    buffer_.insert(buffer_.end(), p, p + n);
  }

  const std::vector<byte>& bytes() const noexcept { return buffer_; }
  std::vector<byte>& bytes() noexcept { return buffer_; }
  std::size_t size() const noexcept { return buffer_.size(); }
  void clear() noexcept { buffer_.clear(); }

private:
  std::vector<byte> buffer_;
};

// A growable in-memory byte buffer that is both a Reader and a Writer.
// This is the modern equivalent of libzpaq::StringBuffer.
class Buffer final : public Reader, public Writer {
public:
  explicit Buffer(std::size_t reserve = 0) {
    data_.reserve(reserve);
  }

  // ---- Writer ----
  void put(int c) override { data_.push_back(static_cast<byte>(c)); }
  void write(const char* buf, std::size_t n) override {
    const byte* p = reinterpret_cast<const byte*>(buf);
    data_.insert(data_.end(), p, p + n);
  }

  // ---- Reader ----
  int get() override {
    return read_pos_ < data_.size() ? data_[read_pos_++] : -1;
  }
  std::size_t read(char* buf, std::size_t n) override {
    const std::size_t avail = data_.size() - read_pos_;
    const std::size_t take = std::min(n, avail);
    std::memcpy(buf, data_.data() + read_pos_, take);
    read_pos_ += take;
    return take;
  }

  // ---- Accessors ----
  byte* data() noexcept { return data_.data(); }
  const byte* data() const noexcept { return data_.data(); }
  std::size_t size() const noexcept { return data_.size(); }
  std::size_t remaining() const noexcept { return data_.size() - read_pos_; }
  void resize(std::size_t n) { data_.resize(n); }
  void reset() {
    data_.clear();
    read_pos_ = 0;
  }
  void rewind() noexcept { read_pos_ = 0; }
  void swap(Buffer& other) noexcept {
    data_.swap(other.data_);
    std::swap(read_pos_, other.read_pos_);
  }
  std::string_view view() const noexcept {
    return {reinterpret_cast<const char*>(data_.data()), data_.size()};
  }

private:
  std::vector<byte> data_;
  std::size_t read_pos_ = 0;
};

} // namespace zpaq_ng::io

#endif // ZPAQ_NG_IO_STREAMS_HPP