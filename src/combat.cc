#include "src/combat.h"

#include <cmath>
#include <limits>
#include <vector>

namespace ms {
namespace {

// Hidden base crit damage not shown in the stat menu; total crit damage is this
// plus the displayed bonus.
constexpr double kBaseCritDamage = 0.35;

// Bosses take half elemental damage by default; ignore-elemental-resistance
// claws it back via 0.5 * (1 + ier).
constexpr double kBossElementalBase = 0.5;

// Attack speed: delay = base * (kSpeedBase - stage) / kSpeedDivisor, then
// ceil'd to whole kTickMs ticks.
constexpr int kSpeedBase = 20;
constexpr int kSpeedDivisor = 16;
constexpr int kTickMs = 30;

// EquipStats stores boss_damage / ignore_enemy_defense as whole percents.
constexpr double kPercentToFraction = 100.0;

// Basic-attack swing animation for a one-handed weapon at the stage-4 (x1.0)
// reference. 800ms is the long-standing engine animation constant; modern GMS
// doesn't republish basic-attack timing (basic attacks are vestigial vs
// skills). Two-handed's swing/stab alternation is deferred until such a weapon
// exists.
constexpr int kOneHandedBaseAttackDelayMs = 800;

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

OffenseStats OffenseStatsFor(Job job, const AllocatedStats& allocated,
                             const EquipStats& equipped) {
  OffenseStats offense;
  // Primary/secondary stat by job; unknown jobs fall through to 0, matching
  // MainStatValue in equipped_panel.
  switch (job) {
    case JOB_WARRIOR:
    case JOB_BEGINNER:
      // STR primary, DEX secondary.
      offense.primary = allocated.str() + equipped.str();
      offense.secondary = allocated.dex() + equipped.dex();
      break;
    default:
      break;
  }
  offense.attack = equipped.attack();
  offense.boss_pct = equipped.boss_damage() / kPercentToFraction;
  offense.ied = equipped.ignore_enemy_defense() / kPercentToFraction;
  return offense;
}

double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob) {
  double mob_pdr = mob.pdr() / kPercentToFraction;
  bool is_boss = mob.boss();

  double stat_value = 4.0 * offense.primary + offense.secondary;
  double max_base = stat_value * offense.attack / 100.0;
  double damage = max_base * (1.0 + offense.mastery) / 2.0;

  damage *= offense.lines * offense.skill_pct;
  damage *= 1.0 + offense.damage_pct + (is_boss ? offense.boss_pct : 0.0);
  damage *= 1.0 + offense.crit_rate * (offense.crit_dmg + kBaseCritDamage);
  damage *= 1.0 + offense.final_dmg_pct;
  damage *= 1.0 - mob_pdr * (1.0 - offense.ied);
  if (is_boss) {
    damage *= kBossElementalBase * (1.0 + offense.ier);
  }
  // Level penalty (final step) deferred; multiplier is 1.0 for now.
  return damage;
}

double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage) {
  double raw_ms = base_delay_ms * (kSpeedBase - attack_speed_stage) /
                  static_cast<double>(kSpeedDivisor);
  double ticks = std::ceil(raw_ms / kTickMs);
  return ticks * kTickMs / 1000.0;
}

int BaseAttackDelayMs(EquipType equip_type) {
  switch (equip_type) {
    case EQUIP_TYPE_ONE_HANDED_SWORD:
      return kOneHandedBaseAttackDelayMs;
    default:
      // Fail safe: fall back to the one-handed swing until other weapon types
      // are added, keeping the swing interval non-zero.
      return kOneHandedBaseAttackDelayMs;
  }
}

double KillCycleSeconds(double damage_per_hit, double swing_interval_seconds,
                        const Mob& mob, double respawn_interval_seconds) {
  if (damage_per_hit <= 0.0) {
    return std::numeric_limits<double>::infinity();  // Never killed.
  }
  // Discrete hits: overkill on the last swing is wasted, no overflow to the
  // next mob.
  double hits_to_kill = std::ceil(mob.max_hp() / damage_per_hit);
  double kill_time = hits_to_kill * swing_interval_seconds;
  // Respawns land only on tick boundaries, so a kill that spills past a tick
  // wastes the rest of it: round up to a whole number of ticks.
  double ticks = std::ceil(kill_time / respawn_interval_seconds);
  return ticks * respawn_interval_seconds * kGameSpeedFactor;
}

std::vector<double> MapKillPeriods(const OffenseStats& offense,
                                   const EquipPrototype& weapon,
                                   const std::vector<const Mob*>& mobs,
                                   int spawn_count,
                                   double respawn_interval_seconds) {
  double swing_interval = SwingIntervalSeconds(
      BaseAttackDelayMs(weapon.equip_type()), weapon.attack_speed());
  double slots_per_type =
      mobs.empty() ? 0.0 : static_cast<double>(spawn_count) / mobs.size();

  std::vector<double> periods;
  periods.reserve(mobs.size());
  for (const Mob* mob : mobs) {
    double cycle =
        KillCycleSeconds(ExpectedAttackDamage(offense, *mob), swing_interval,
                         *mob, respawn_interval_seconds);
    // The type's slots cycle in parallel, so the map-level period shrinks by
    // the slot share; zero slots (or an unkillable mob) leave it at +inf.
    periods.push_back(slots_per_type > 0.0
                          ? cycle / slots_per_type
                          : std::numeric_limits<double>::infinity());
  }
  return periods;
}

int64_t FlushKills(double period_seconds, double elapsed_seconds,
                   double* accumulator) {
  if (!std::isfinite(period_seconds) || period_seconds <= 0.0) {
    return 0;
  }
  *accumulator += elapsed_seconds / period_seconds;
  double whole = std::floor(*accumulator);
  *accumulator -= whole;
  return static_cast<int64_t>(whole);
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

}  // namespace ms
