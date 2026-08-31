#include "src/character/honor.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "src/character/inner_ability.h"

namespace ms {
namespace {

// What a level-up pays below the first band, and what each band above it adds.
constexpr int64_t kBaseLevelHonor = 700;
constexpr int64_t kHonorPerBand = 100;

// The band the base is paid through: every level up to and including 59.
constexpr int kFirstBandLevel = 60;

}  // namespace

int64_t HonorForLevelUp(int level) {
  if (level < 2) {
    return 0;  // nothing climbs to level 1
  }
  int bands = std::max(0, level / 10 - kFirstBandLevel / 10 + 1);
  return kBaseLevelHonor + bands * kHonorPerBand;
}

int64_t HonorForLevels(int from_level, int to_level) {
  int64_t total = 0;
  for (int level = std::max(from_level + 1, 2); level <= to_level; ++level) {
    total += HonorForLevelUp(level);
  }
  return total;
}

bool HonorVisible(int character_level, int account_level) {
  return std::max(character_level, account_level) >= kInnerAbilityUnlockLevel;
}

int64_t RollMobHonor(int64_t kills, std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  std::binomial_distribution<int64_t> paying(kills, kMobHonorChance);
  return paying(rng) * kMobHonorPerDrop;
}

}  // namespace ms
