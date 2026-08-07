/* How hard, and how often, the player hits, and how hard a mob hits back: the
 * GMS damage formulas and the attack-speed timing that feeds them. Pure math
 * over a character's stats and a mob -- no game state, no notion of a fight in
 * progress.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_H_
#define MS_SRC_COMBAT_DAMAGE_H_

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Offensive parameters feeding the GMS damage formula. Modifier fields default
// to their identity (no-effect) value; real values graduate in one at a time as
// gear, skills, etc. produce them.
struct OffenseStats {
  int primary = 0;             // total primary stat
  int secondary = 0;           // total secondary stat
  int attack = 0;              // total weapon/gear attack
  int level = 0;               // attacker level, for the level multiplier
  double mastery = 0.15;       // 0..1; raises min damage (beginner placeholder)
  double skill_pct = 1.0;      // skill damage multiplier (1.0 == 100%)
  int lines = 1;               // hits per attack
  double damage_pct = 0.0;     // additive %dmg, as fraction
  double boss_pct = 0.0;       // additive boss %dmg; applies only vs bosses
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // crit damage bonus, atop the hidden 0.35 base
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  double ier = 0.0;            // ignore elemental resistance, 0..1
};

// Whether a skill of this kind carries a damage multiplier of its own -- the
// swings and the things that fire on their own clock. The rest either fold
// into the character's stats or do nothing we model.
bool DealsDamage(SkillKind kind);

// What a character's learned passives add to every swing, whichever attack
// they end up choosing. Kept together rather than passed one at a time: the
// list grows with each job book, and none of it depends on the target.
// DerivedStatsFor produces the values; see character_stats.h.
struct PassiveOffense {
  double crit_rate = 0.0;  // added chance for a swing to crit (0.40 == 40%)
  // Weapon mastery, 0..1. 0 means the character has no mastery skill and
  // keeps the beginner's baseline: a mastery skill's first level is worth
  // less than that, and learning a skill must never make a swing worse.
  double mastery = 0.0;
};

// Builds OffenseStats from a character's job, level and summed stats. The job
// picks primary/secondary; attack, boss_pct and ied come from gear.
//
// `attack_skill` is the learned attack being swung, at `attack_level`, or null
// for the bare 100% poke. Choosing WHICH attack is the caller's job -- it
// depends on how many mobs are up, which this pure per-mob math cannot see.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped,
                             const Skill* attack_skill, int attack_level,
                             const PassiveOffense& passives = {});

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// One number for "how hard this character hits", for comparing characters
// rather than predicting a swing: the damage chain with everything that
// depends on the target, the skill or the moment stripped out. Build the stats
// with a null attack skill, since skill_pct, lines, ied, ier and level are all
// ignored here.
//
// Unlike GMS, crit is weighted by its rate: a flat `1.35 + crit damage` would
// price critical damage the same whether it lands every swing or never.
int CombatPower(const OffenseStats& offense);

// What a character brings to being hit. The defensive mirror of OffenseStats:
// DerivedStatsFor produces both fields, and the character's own level decides
// how much of the DEF actually counts.
struct DefenseStats {
  int level = 0;
  int def = 0;
  // The share of incoming damage cancelled after the formula below has run
  // (0.10 == 10% less taken).
  double damage_taken_pct = 0.0;
};

// Expected damage of one hit from `mob` -- min and max rolls averaged, no RNG.
// Never less than 1, as in GMS.
//
// DEF subtracts flatly from the mob's attack, but can never cancel more than
// 80% of it. That cap is the shape of the whole thing: on a map near the
// character's level their DEF clears it and more armour buys nothing, and on
// one far above it the cap never binds and the hit lands close to full.
double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob);

// The GMS level multiplier: 1.1 at the monster's level, rising to 1.2 at +5
// and beyond, and falling to 0 at 40 levels under it.
double LevelMultiplier(int player_level, int mob_level);

// Seconds between swings: base_delay_ms * (20 - stage) / 16, rounded up to
// whole kTickMs units. Stage 1..10, 10 fastest, 4 == base.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Base swing animation (ms) for a weapon type -- the stage-4 reference scaled
// by SwingIntervalSeconds. A weapon-class property, not per-item.
int BaseAttackDelayMs(EquipType equip_type);

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
