#pragma once

#include <cstdint>

namespace torrview {

struct CachePolicy {
  static constexpr std::int64_t mib = 1024LL * 1024LL;

  std::int64_t max_bytes = 512LL * mib;
  std::int64_t retain_before_bytes = 128LL * mib;
  std::int64_t retain_after_bytes = 384LL * mib;

  static CachePolicy from_limit_mib(int limit_mib);
  [[nodiscard]] int limit_mib() const;
};

struct PieceWindow {
  int first_piece = 0;
  int end_piece = 0;

  [[nodiscard]] bool empty() const;
  [[nodiscard]] bool contains(int piece) const;
};

[[nodiscard]] CachePolicy normalize_cache_policy(CachePolicy policy);
[[nodiscard]] PieceWindow compute_retained_piece_window(std::int64_t byte_offset,
                                                        int total_pieces,
                                                        int piece_length,
                                                        CachePolicy policy);

} // namespace torrview
