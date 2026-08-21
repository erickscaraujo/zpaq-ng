// concepts.hpp - C++20 concepts used across ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// These concepts encode the interfaces of the byte stream abstraction and the
// hasher family. Using concepts at the API boundary keeps template diagnostics
// readable and prevents silent mismatches between modules.

#ifndef ZPAQ_NG_CORE_CONCEPTS_HPP
#define ZPAQ_NG_CORE_CONCEPTS_HPP

#include <cstddef>
#include <concepts>

namespace zpaq_ng::concepts {

// A byte source. Must be able to return the next byte (0..255) or -1 at EOF.
template <typename T>
concept Reader = requires(T& r, char* buf, std::size_t n) {
  { r.get() } -> std::convertible_to<int>;
  { r.read(buf, n) } -> std::convertible_to<std::size_t>;
};

// A byte sink.
template <typename T>
concept Writer = requires(T& w, int c, const char* buf, std::size_t n) {
  { w.put(c) } -> std::same_as<void>;
  { w.write(buf, n) } -> std::same_as<void>;
};

// An incremental hash / checksum.
template <typename T>
concept Hasher = requires(T& h, int c, const char* buf, std::size_t n) {
  { h.put(c) } -> std::same_as<void>;
  { h.write(buf, n) } -> std::same_as<void>;
  { h.size() } -> std::unsigned_integral;
  { h.result() } -> std::same_as<const char*>;
};

} // namespace zpaq_ng::concepts

#endif // ZPAQ_NG_CORE_CONCEPTS_HPP