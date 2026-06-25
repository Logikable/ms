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

// Our game runs this many times slower than GMS; the one global pacing knob.
constexpr double kGameSpeedFactor = 10.0;

// EquipStats stores boss_damage / ignore_enemy_defense as whole percents.
constexpr double kPercentToFraction = 100.0;

// Basic-attack swing animation for a one-handed weapon at the stage-4 (x1.0)
// reference. 800ms is the long-standing engine animation constant; modern GMS
// doesn't republish basic-attack timing (basic attacks are vestigial vs
// skills). Two-handed's swing/stab alternation is deferred until such a weapon
// exists.
constexpr int kOneHandedBaseAttackDelayMs = 800;

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

}  // namespace ms
