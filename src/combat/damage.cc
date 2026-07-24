#include "src/combat/damage.h"

#include <algorithm>
#include <cmath>

#include "src/combat/constants.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

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

// EquipStats stores boss_damage / ignore_enemy_defense as whole percents.
constexpr double kPercentToFraction = 100.0;

// Basic-attack swing animation for a one-handed weapon at the stage-4 (x1.0)
// reference. 800ms is the long-standing engine animation constant; modern GMS
// doesn't republish basic-attack timing (basic attacks are vestigial vs
// skills). Two-handed's swing/stab alternation is deferred until such a weapon
// exists.
constexpr int kOneHandedBaseAttackDelayMs = 800;

// Level multiplier: 1.1 at equal level, +0.02 per level above, capped at +5.
constexpr double kEqualLevelMultiplier = 1.1;
constexpr double kAboveLevelStep = 0.02;
constexpr int kAboveLevelCap = 5;

// Level multiplier when the monster out-levels the player, indexed by the gap
// (mob level - player level), 1..39; a gap of 40+ is 0 (output floored to 1
// damage). The -37..-39 rows read 0.8/0.5/0.3 on the wiki, which breaks the
// otherwise monotone decline -- taken here as 0.08/0.05/0.03, a dropped leading
// zero. That tail is unreachable with current content (worst gap is -19).
constexpr double kUnderLevelMultiplier[] = {
    0.0,                           // gap 0 unused (see LevelMultiplier)
    1.0584, 1.007, 0.9672, 0.918,  // -1..-4
    0.88,   0.85,  0.83,   0.8,    // -5..-8
    0.78,   0.75,  0.73,   0.7,    // -9..-12
    0.68,   0.65,  0.63,   0.6,    // -13..-16
    0.58,   0.55,  0.53,   0.5,    // -17..-20
    0.48,   0.45,  0.43,   0.4,    // -21..-24
    0.38,   0.35,  0.33,   0.3,    // -25..-28
    0.28,   0.25,  0.23,   0.2,    // -29..-32
    0.18,   0.15,  0.13,   0.1,    // -33..-36
    0.08,   0.05,  0.03,           // -37..-39
};
constexpr int kMaxUnderLevelGap = 39;

}  // namespace

double LevelMultiplier(int player_level, int mob_level) {
  int diff = player_level - mob_level;
  if (diff >= 0) {
    // At or above the monster's level: a bonus that stops growing past +5.
    return kEqualLevelMultiplier +
           kAboveLevelStep * std::min(diff, kAboveLevelCap);
  }
  int gap = -diff;
  if (gap > kMaxUnderLevelGap) {
    return 0.0;  // Caller floors output to 1 damage.
  }
  return kUnderLevelMultiplier[gap];
}

OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped,
                             const Skill* attack_skill, int attack_level) {
  OffenseStats offense;
  offense.level = level;
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
  // The learned attack skill's multiplier replaces the bare 100% poke. Effect
  // at level L is base + per_level*(L-1). Passive skills fold elsewhere (their
  // levers are all defensive -- see DerivedStatsFor), so they are ignored here.
  if (attack_skill != nullptr && attack_skill->kind() == SKILL_KIND_ATTACK) {
    offense.skill_pct =
        attack_skill->base().skill_pct() +
        attack_skill->per_level().skill_pct() * (attack_level - 1);
    // A multi-hit skill strikes each target this many times per swing, so its
    // per-target damage is skill_pct once per line.
    offense.lines = std::max(1, attack_skill->lines());
  }
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
  double level_mult = LevelMultiplier(offense.level, mob.level());
  if (level_mult <= 0.0) {
    return 1.0;  // 40+ levels under the mob: output is floored to 1 damage.
  }
  return damage * level_mult;
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

}  // namespace ms
