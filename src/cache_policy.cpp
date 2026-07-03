#include "cache_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace torrview {
namespace {

constexpr std::int64_t min_cache_bytes = 1;

std::int64_t clamp_non_negative(std::int64_t value) { return std::max<std::int64_t>(0, value); }

} // namespace

CachePolicy CachePolicy::from_limit_mib(int limit_mib) {
  const int clamped_mib = std::clamp(limit_mib, 1, 1024 * 1024);
  CachePolicy policy;
  policy.max_bytes = static_cast<std::int64_t>(clamped_mib) * mib;
  policy.retain_before_bytes = policy.max_bytes / 4;
  policy.retain_after_bytes = policy.max_bytes - policy.retain_before_bytes;
  return policy;
}

int CachePolicy::limit_mib() const {
  return static_cast<int>(std::llround(static_cast<double>(max_bytes) /
                                       static_cast<double>(mib)));
}

bool PieceWindow::empty() const { return first_piece >= end_piece; }

bool PieceWindow::contains(int piece) const {
  return piece >= first_piece && piece < end_piece;
}

CachePolicy normalize_cache_policy(CachePolicy policy) {
  policy.max_bytes = std::max(policy.max_bytes, min_cache_bytes);
  policy.retain_before_bytes = clamp_non_negative(policy.retain_before_bytes);
  policy.retain_after_bytes = clamp_non_negative(policy.retain_after_bytes);

  const std::int64_t retained_total = policy.retain_before_bytes + policy.retain_after_bytes;
  if (retained_total == 0) {
    policy.retain_after_bytes = policy.max_bytes;
    return policy;
  }

  if (retained_total > policy.max_bytes) {
    policy.retain_before_bytes =
        (policy.max_bytes * policy.retain_before_bytes) / retained_total;
    policy.retain_after_bytes = policy.max_bytes - policy.retain_before_bytes;
  }
  return policy;
}

PieceWindow compute_retained_piece_window(std::int64_t byte_offset, int total_pieces,
                                          int piece_length, CachePolicy policy) {
  if (total_pieces <= 0 || piece_length <= 0) {
    return {};
  }

  policy = normalize_cache_policy(policy);
  const std::int64_t clamped_offset = clamp_non_negative(byte_offset);
  const std::int64_t first_byte =
      std::max<std::int64_t>(0, clamped_offset - policy.retain_before_bytes);
  const std::int64_t end_byte = clamped_offset + std::max<std::int64_t>(1, policy.retain_after_bytes);

  PieceWindow window;
  window.first_piece = static_cast<int>(
      std::clamp<std::int64_t>(first_byte / piece_length, 0, total_pieces));
  window.end_piece = static_cast<int>(
      std::clamp<std::int64_t>((end_byte + piece_length - 1) / piece_length, 0, total_pieces));

  if (window.end_piece <= window.first_piece) {
    window.end_piece = std::min(total_pieces, window.first_piece + 1);
  }
  return window;
}

} // namespace torrview
