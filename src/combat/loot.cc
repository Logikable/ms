#include "src/combat/loot.h"

#include <cmath>
#include <cstdint>

#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Every mob has this base chance to drop meso on death. Item drop rate raises
// it (deferred), so for now it is the flat expected fraction of kills yielding
// meso.
constexpr double kMesoDropChance = 0.60;

// The level gap (either direction) within which meso is unpenalized.
constexpr int kMesoPenaltyFreeGap = 10;

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

double MesoLevelPenalty(int level_difference) {
  if (level_difference >= -kMesoPenaltyFreeGap &&
      level_difference <= kMesoPenaltyFreeGap) {
    return 1.0;
  }
  if (level_difference > kMesoPenaltyFreeGap) {
    // Player out-levels the mob; the steepest penalty.
    if (level_difference >= 30) {
      return 0.0;
    }
    if (level_difference <= 20) {
      return 1.0 - 0.02 * (level_difference - 10);
    }
    // 21..29 follow an irregular reduction table (percent reduced).
    const int reduction[] = {25, 31, 38, 46, 55, 65, 76, 83, 97};
    return 1.0 - reduction[level_difference - 21] / 100.0;
  }
  // Mob out-levels the player; a gentler penalty than the reverse.
  int below = -level_difference;
  if (below >= 34) {
    return 0.0;
  }
  if (below <= 20) {
    return 1.0 - 0.03 * (below - 10);
  }
  return 1.0 - (0.30 + 0.05 * (below - 20));
}

double ExpectedMesoPerKill(const Mob& mob, int player_level) {
  int mob_level = mob.level();
  // A level-1 mob drops a flat 1 meso; all higher levels scale by the band
  // mean.
  double base_amount =
      mob_level <= 1 ? 1.0 : mob_level * MeanMesoMultiplier(mob_level);
  return kMesoDropChance * base_amount *
         MesoLevelPenalty(player_level - mob_level);
}

int64_t FlushDrops(double per_kill, int64_t kills, double* accumulator) {
  if (!std::isfinite(per_kill) || per_kill <= 0.0 || kills <= 0) {
    return 0;
  }
  *accumulator += per_kill * static_cast<double>(kills);
  double whole = std::floor(*accumulator);
  *accumulator -= whole;
  return static_cast<int64_t>(whole);
}

}  // namespace ms
