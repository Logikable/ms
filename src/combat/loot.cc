#include "src/combat/loot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Chance a mob drops meso on death before any drop rate is added.
constexpr double kBaseMesoDropChance = 0.60;

// How far from the average the multiplier can land. Every GMS band is its
// average plus or minus 20%.
constexpr double kMesoSpread = 0.2;

// Heroic worlds multiply every meso drop by this. GMS gives it as a beginner
// passive because Heroic worlds have no trading and buy with meso what other
// worlds buy with cash. We have no trading either.
constexpr double kHeroicMesoMultiplier = 6.0;

// Average meso multiplier for the mob's level band; a drop is mob_level times
// this. Values are the midpoints of GMS's per-band ranges.
double MeanMesoMultiplier(int mob_level) {
  if (mob_level <= 20) {
    return 2.0;
  } else if (mob_level <= 30) {
    return 2.5;
  } else if (mob_level <= 40) {
    return 3.0;
  } else if (mob_level <= 50) {
    return 3.5;
  } else if (mob_level <= 60) {
    return 5.0;
  } else if (mob_level <= 70) {
    return 6.0;
  } else if (mob_level <= 80) {
    return 6.5;
  } else if (mob_level <= 90) {
    return 7.0;
  } else {
    return 7.5;
  }
}

}  // namespace

double MesoDropChance(double item_drop_pct) {
  if (!std::isfinite(item_drop_pct) || item_drop_pct <= 0.0) {
    return kBaseMesoDropChance;
  }
  return std::min(1.0, kBaseMesoDropChance * (1.0 + item_drop_pct));
}

double MeanMesoPerDrop(const Mob& mob) {
  int mob_level = mob.level();
  // A level-1 mob drops 1 meso; higher levels scale by the band average.
  double base_amount =
      mob_level <= 1 ? 1.0 : mob_level * MeanMesoMultiplier(mob_level);
  return kHeroicMesoMultiplier * base_amount;
}

double ExpectedMesoPerKill(const Mob& mob, double item_drop_pct) {
  return MesoDropChance(item_drop_pct) * MeanMesoPerDrop(mob);
}

int64_t RollDrops(double per_kill, int64_t kills, std::mt19937& rng) {
  if (!std::isfinite(per_kill) || per_kill <= 0.0 || kills <= 0) {
    return 0;
  }
  // A rate above 1 drops every kill plus a chance at another, so pay the whole
  // part outright and roll only the remainder.
  double whole = std::floor(per_kill);
  int64_t dropped = static_cast<int64_t>(whole) * kills;
  double chance = per_kill - whole;
  if (chance > 0.0) {
    std::binomial_distribution<int64_t> flips(kills, chance);
    dropped += flips(rng);
  }
  return dropped;
}

double BossDropRate(const MobDrop& drop, double item_drop_pct) {
  double per_kill = drop.per_kill();
  if (!std::isfinite(per_kill) || per_kill <= 0.0) {
    return 0.0;
  }
  bool lifts = std::isfinite(item_drop_pct) && item_drop_pct > 0.0;
  if (!drop.has_equip()) {
    // Stackable items get more copies, so the whole rate is multiplied.
    return lifts ? per_kill * (1.0 + item_drop_pct) : per_kill;
  }
  double whole = std::floor(per_kill);
  double chance = per_kill - whole;
  if (lifts) {
    chance = std::min(1.0, chance * (1.0 + item_drop_pct));
  }
  return whole + chance;
}

int64_t RollMeso(const Mob& mob, int64_t kills, double item_drop_pct,
                 std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  // Roll the drop chance once for the whole batch; nothing needs to know which
  // kills paid.
  std::binomial_distribution<int64_t> paying(kills,
                                             MesoDropChance(item_drop_pct));
  int64_t drops = paying(rng);
  int mob_level = mob.level();
  if (mob_level <= 1) {
    // 1 meso per drop, before the world multiplier.
    return static_cast<int64_t>(drops * kHeroicMesoMultiplier);
  }
  double mean = MeanMesoMultiplier(mob_level);
  std::uniform_real_distribution<double> multiplier(mean * (1.0 - kMesoSpread),
                                                    mean * (1.0 + kMesoSpread));
  // Roll each drop separately, since each has its own amount. A tick covers a
  // few dozen kills and a sim's longest step a few hundred, so this is cheap.
  int64_t total = 0;
  for (int64_t i = 0; i < drops; ++i) {
    total += std::llround(mob_level * multiplier(rng) * kHeroicMesoMultiplier);
  }
  return total;
}

}  // namespace ms
