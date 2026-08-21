// matching.cpp - Match finder implementations and evaluation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "matching/matching.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "chunking/chunking.hpp"

namespace zpaq_ng::matching {

namespace {
constexpr std::uint32_t hash4(const byte* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}
} // namespace

HashChainFinder::HashChainFinder(std::size_t window) : window_(window) {
  head_.assign(1u << 16, 0);  // indexed by (hash4 ^ (hash4>>16)) & 0xffff
}

std::vector<Match> HashChainFinder::find_greedy(ConstBytes data,
                                                std::size_t min_len,
                                                std::size_t max_depth,
                                                std::size_t max_len) {
  std::vector<Match> out;
  const std::size_t n = data.size();
  if (n < 4) return out;
  prev_.assign(n + 1, 0);
  // Precompute the compressed hash for every position (n-3 entries).
  std::vector<std::uint32_t> hs(n - 3);
  for (std::size_t i = 0; i + 3 < n; ++i) {
    const std::uint32_t h = hash4(data.data() + i);
    hs[i] = (h ^ (h >> 16)) & 0xffff;
  }

  std::size_t pos = 0;
  while (pos + 3 < n) {
    const std::uint32_t key = hs[pos];
    // Walk the chain looking for the longest match.
    std::size_t best_len = 0, best_dist = 0;
    std::uint32_t cand = head_[key];
    std::size_t depth = 0;
    while (cand != 0 && depth < max_depth) {
      const std::size_t cp = cand - 1;  // 0-based position
      if (pos > cp && pos - cp <= window_ && cp + best_len < n &&
          data[cp + best_len] == data[pos + best_len]) {
        std::size_t l = 0;
        while (pos + l < n && cp + l < n && data[cp + l] == data[pos + l] &&
               l < max_len)
          ++l;
        if (l > best_len) {
          best_len = l;
          best_dist = pos - cp;
        }
      }
      cand = prev_[cp + 1];
      ++depth;
    }
    // Insert this position into the chain for future positions.
    prev_[pos + 1] = head_[key];
    head_[key] = static_cast<std::uint32_t>(pos + 1);

    if (best_len >= min_len && best_dist > 0) {
      out.push_back({pos, best_len, best_dist});
      // Greedy: skip past the match, inserting skipped positions too so the
      // dictionary stays populated.
      for (std::size_t k = pos + 1; k < pos + best_len; ++k) {
        if (k + 3 < n) {
          prev_[k + 1] = head_[hs[k]];
          head_[hs[k]] = static_cast<std::uint32_t>(k + 1);
        }
      }
      pos += best_len;
    } else {
      ++pos;
    }
  }
  return out;
}

FinderResult benchmark_hash_chain(ConstBytes data) {
  FinderResult r;
  r.name = "HashChain";
  r.input = data.size();
  r.memory = (1u << 16) * 4 + (data.size() + 1) * 4;
  const auto t0 = std::chrono::steady_clock::now();
  HashChainFinder f(1u << 24);
  const auto matches = f.find_greedy(data, 4, 64, 256);
  r.seconds = std::chrono::duration<double>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
  r.matches = matches.size();
  for (const auto& m : matches) r.match_bytes += m.len;
  return r;
}

FinderResult benchmark_rolling(ConstBytes data) {
  FinderResult r;
  r.name = "RollingHash";
  r.input = data.size();
  r.memory = data.size() * 4;
  const auto t0 = std::chrono::steady_clock::now();
  chunking::RollingHash rh(16);
  std::size_t matches = 0, bytes = 0;
  // Look for 16-byte windows that recur: count positions whose rolling hash
  // repeats within a 1 MiB window.
  std::vector<std::uint32_t> seen(1u << 20, 0);
  std::size_t n = data.size();
  for (std::size_t i = 0; i + 16 <= n; ++i) {
    const std::uint64_t h = rh.push(data[i]);
    const std::uint32_t slot = static_cast<std::uint32_t>(h) & 0xfffff;
    if (seen[slot] != 0) {
      ++matches;
      bytes += 16;
    }
    seen[slot] = static_cast<std::uint32_t>(i + 1);
  }
  r.seconds = std::chrono::duration<double>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
  r.matches = matches;
  r.match_bytes = bytes;
  return r;
}

} // namespace zpaq_ng::matching