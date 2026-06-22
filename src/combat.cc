#include "src/combat.h"

#include <algorithm>
#include <cmath>

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

}  // namespace

double ExpectedAttackDamage(const OffenseStats& offense, double mob_pdr,
                            bool is_boss) {
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

double Dps(const OffenseStats& offense, double mob_pdr, bool is_boss,
           int base_delay_ms, int attack_speed_stage) {
  return ExpectedAttackDamage(offense, mob_pdr, is_boss) /
         SwingIntervalSeconds(base_delay_ms, attack_speed_stage);
}

double KillsPerSecond(double raw_dps, int mob_hp, int max_targets,
                      double spawn_per_second) {
  double dps_rate = raw_dps * max_targets / mob_hp;
  return std::min(dps_rate, spawn_per_second) / kGameSpeedFactor;
}

double ExpPerSecond(double kills_per_second, int64_t mob_exp) {
  return kills_per_second * mob_exp;
}

}  // namespace ms
