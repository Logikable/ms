#include "src/character/v_matrix.h"

#include <algorithm>
#include <cstdint>
#include <random>

namespace ms {

double VPointsPerKill(double item_drop_pct) {
  return std::min(1.0,
                  kVPointDropChance * (1.0 + std::max(0.0, item_drop_pct)));
}

int64_t RollMobVPoints(int64_t kills, double item_drop_pct, std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  std::binomial_distribution<int64_t> paying(kills,
                                             VPointsPerKill(item_drop_pct));
  return paying(rng);
}

}  // namespace ms
