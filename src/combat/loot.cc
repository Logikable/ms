#include "src/combat/loot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Chance a mob drops meso on death before any drop rate is added.
constexpr double kBaseMesoDropChance = 0.60;

// How far either side of the mean the multiplier is drawn. Every GMS band is
// its mean plus or minus a fifth -- 2-20 runs 1.6 to 2.4, 91+ runs 6 to 9 --
// so one spread covers the table.
constexpr double kMesoSpread = 0.2;

// What a Heroic world multiplies every meso drop by. GMS hands one out as a
// Novice passive: a world with no trading has to buy with meso what an
// Interactive world buys for cash, so the drops are worth six times as much.
// We have no trading either, which makes Heroic the world we already are.
constexpr double kHeroicMesoMultiplier = 6.0;

// Mean of the mob's randomized meso multiplier k, chosen by the level band the
// mob falls in; the dropped amount is mob_level * k. Bounds are the midpoints
// of the GMS per-band k ranges. Level 1 is a flat 1 meso, handled by the
// caller.
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
  // A level-1 mob drops a flat 1 meso; all higher levels scale by the band
  // mean.
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
  // A rate above one is a drop every kill plus a chance at another, so the
  // whole part is paid outright and only the remainder is a coin to flip.
  double whole = std::floor(per_kill);
  int64_t dropped = static_cast<int64_t>(whole) * kills;
  double chance = per_kill - whole;
  if (chance > 0.0) {
    std::binomial_distribution<int64_t> flips(kills, chance);
    dropped += flips(rng);
  }
  return dropped;
}

double BossDropRate(double per_kill, double item_drop_pct) {
  if (!std::isfinite(per_kill) || per_kill <= 0.0) {
    return 0.0;
  }
  double whole = std::floor(per_kill);
  double chance = per_kill - whole;
  if (std::isfinite(item_drop_pct) && item_drop_pct > 0.0) {
    chance = std::min(1.0, chance * (1.0 + item_drop_pct));
  }
  return whole + chance;
}

int64_t RollMeso(const Mob& mob, int64_t kills, double item_drop_pct,
                 std::mt19937& rng) {
  if (kills <= 0) {
    return 0;
  }
  // The drop chance is one roll over the batch: which of these kills paid at
  // all is not a question anything downstream asks.
  std::binomial_distribution<int64_t> paying(kills,
                                             MesoDropChance(item_drop_pct));
  int64_t drops = paying(rng);
  int mob_level = mob.level();
  if (mob_level <= 1) {
    // A flat 1 meso each before the world rate.
    return static_cast<int64_t>(drops * kHeroicMesoMultiplier);
  }
  double mean = MeanMesoMultiplier(mob_level);
  std::uniform_real_distribution<double> multiplier(mean * (1.0 - kMesoSpread),
                                                    mean * (1.0 + kMesoSpread));
  // Rolled one drop at a time, because each drop is its own amount. A tick
  // pays for a few dozen kills and a sim's longest step for a few hundred.
  int64_t total = 0;
  for (int64_t i = 0; i < drops; ++i) {
    total += std::llround(mob_level * multiplier(rng) * kHeroicMesoMultiplier);
  }
  return total;
}

}  // namespace ms
