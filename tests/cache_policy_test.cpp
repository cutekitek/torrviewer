#include "cache_policy.hpp"

#include <cstdlib>
#include <iostream>

void require(bool value) {
  if (!value) {
    std::cerr << "Cache policy invariant failed\n";
    std::exit(1);
  }
}

int main() {
  using namespace torrview;
  for (const int limit : {16, 64, 128, 512}) {
    const auto policy = CachePolicy::from_limit_mib(limit);
    require(policy.max_bytes == limit * CachePolicy::mib);
    require(policy.retain_before_bytes + policy.retain_after_bytes == policy.max_bytes);
    for (const std::int64_t offset :
         {0LL, 1LL, 64LL * CachePolicy::mib, 1024LL * CachePolicy::mib}) {
      const auto window = compute_retained_piece_window(offset, 4096, 256 * 1024, policy);
      require(window.first_piece >= 0 && window.end_piece <= 4096);
      require(window.first_piece <= window.end_piece);
      require(static_cast<std::int64_t>(window.end_piece - window.first_piece) * 256 * 1024 <=
              policy.max_bytes + 2 * 256 * 1024);
    }
  }
  require(compute_retained_piece_window(0, 0, 16384, {}).empty());
  require(compute_retained_piece_window(0, 5, 0, {}).empty());
  require(CachePolicy::from_limit_mib(-1).limit_mib() == 1);
}
