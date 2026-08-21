// error.hpp - Error handling policy for ZPAQ Next Generation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Design decision: the original libzpaq reports fatal errors through a global
// error() callback that never returns. That design predates C++ exceptions and
// makes safe resource handling difficult. ZPAQ-NG instead throws
// zpaq_error, which is caught at module boundaries (CLI, archive IO). This
// keeps error handling composable and RAII-safe while remaining compatible
// with the original format.
//
// The legacy free function libzpaq::error is intentionally NOT reproduced;
// callers that must interoperate with the original API should adapt at the
// integration layer.

#ifndef ZPAQ_NG_CORE_ERROR_HPP
#define ZPAQ_NG_CORE_ERROR_HPP

#include <stdexcept>
#include <string>

namespace zpaq_ng {

// Base class for every error produced by ZPAQ-NG.
//
// error_code() returns a stable machine readable category:
//   "format"       malformed archive, ZPAQL, or bitstream input
//   "checksum"     integrity verification failed
//   "io"           file system or stream failure
//   "memory"       allocation failed
//   "unsupported"  valid input that this build cannot process
//   "invalid"      invalid programmatic argument
class zpaq_error : public std::runtime_error {
public:
  zpaq_error(std::string category, std::string message)
      : std::runtime_error(std::move(message)), category_(std::move(category)) {}
  const std::string& error_code() const noexcept { return category_; }

private:
  std::string category_;
};

// The input stream or archive is malformed.
class format_error : public zpaq_error {
public:
  explicit format_error(std::string message)
      : zpaq_error("format", std::move(message)) {}
};

// An integrity check (SHA-1, size, CRC) failed.
class checksum_error : public zpaq_error {
public:
  explicit checksum_error(std::string message)
      : zpaq_error("checksum", std::move(message)) {}
};

// An I/O operation failed.
class io_error : public zpaq_error {
public:
  explicit io_error(std::string message)
      : zpaq_error("io", std::move(message)) {}
};

// Memory allocation failed.
class memory_error : public zpaq_error {
public:
  explicit memory_error(std::string message)
      : zpaq_error("memory", std::move(message)) {}
};

// The input is valid but this build cannot process it (e.g. a component
// type that requires resources beyond configured limits).
class unsupported_error : public zpaq_error {
public:
  explicit unsupported_error(std::string message)
      : zpaq_error("unsupported", std::move(message)) {}
};

// An invalid argument or program state was supplied.
class invalid_argument_error : public zpaq_error {
public:
  explicit invalid_argument_error(std::string message)
      : zpaq_error("invalid", std::move(message)) {}
};

} // namespace zpaq_ng

#endif // ZPAQ_NG_CORE_ERROR_HPP