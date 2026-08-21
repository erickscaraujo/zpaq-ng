// matching.hpp - New match finders (experimental layer).
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// The ZPAQ LZ77 preprocessing encodes matches in a specific bit stream that
// the method's PCOMP decodes; the legacy LZBuffer search is required for
// byte-compatible archives. These finders are a separate, benchmarkable layer
// used to evaluate new search strategies (hash chain, rolling hash). A strategy
// is adopted into the pipeline only if measurement shows a real benefit.

#ifndef ZPAQ_NG_MATCHING_MATCHING_HPP
#define ZPAQ_NG_MATCHING_MATCHING_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace zpaq_ng::matching {

struct Match {
  std::size_t pos = 0;    // position in the data
  std::size_t len = 0;    // match length
  std::size_t dist = 0;   // distance to the earlier occurrence
};

// Greedy forward match finder backed by a hash chain. Simple, cache friendly,
// and a good baseline for measuring search strategies.
class HashChainFinder {
public:
  explicit HashChainFinder(std::size_t window = 1u << 24);

  // Greedy match search over data. Appends matches of length >= min_len;
  // positions covered by a match are skipped (greedy parsing). max_depth caps
  // chain walks per position, max_len caps the match-length comparison (both
  // keep pathological inputs from degrading the search to O(n*depth*len)).
  std::vector<Match> find_greedy(ConstBytes data, std::size_t min_len = 4,
                                 std::size_t max_depth = 64,
                                 std::size_t max_len = 4096);

  // Approximate working memory in bytes.
  std::size_t memory_estimate(std::size_t n) const noexcept {
    return head_.size() * sizeof(std::uint32_t) + (n + 1) * sizeof(std::uint32_t);
  }

private:
  std::vector<std::uint32_t> head_;
  mutable std::vector<std::uint32_t> prev_;  // reused scratch
  std::size_t window_;
};

// Evaluation result for one finder on one input.
struct FinderResult {
  std::string name;
  std::size_t input = 0;
  std::size_t matches = 0;
  std::size_t match_bytes = 0;   // input bytes covered by matches
  std::size_t memory = 0;
  double seconds = 0.0;
  double throughput_mbps() const {
    return seconds > 0.0 ? input / 1e6 / seconds : 0.0;
  }
};

// Measure a HashChainFinder greedy search on data (used by `profile`).
FinderResult benchmark_hash_chain(ConstBytes data);

// Measure a simple rolling-hash matcher on data (repeats of small patterns).
FinderResult benchmark_rolling(ConstBytes data);

} // namespace zpaq_ng::matching

#endif // ZPAQ_NG_MATCHING_MATCHING_HPP