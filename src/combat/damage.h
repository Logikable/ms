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
  int primary = 0;          // total primary stat
  int secondary = 0;        // total secondary stat
  int attack = 0;           // total weapon/gear attack
  int level = 0;            // attacker level, for the level multiplier
  double mastery = 0.15;    // 0..1; raises min damage (beginner placeholder)
  double skill_pct = 1.0;   // skill damage multiplier (1.0 == 100%)
  int lines = 1;            // hits per attack
  double damage_pct = 0.0;  // additive %dmg, as fraction
  double boss_pct = 0.0;    // additive boss %dmg; applies only vs bosses
  // Added to skill_pct against anything that is not a boss, so it is worth its
  // value once per line. Comes from the attack being swung rather than from
  // the character, unlike boss_pct -- see SkillEffect::normal_skill_pct.
  double normal_skill_pct = 0.0;
  // Both sit atop every character's base pair in constants.h, which the
  // formula adds; these carry only what gear and skills bought.
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // 0..1
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  double ier = 0.0;            // ignore elemental resistance, 0..1
  // GMS's leading weapon constant; see WeaponConstant. 1.0 is the identity a
  // bare stat line carries, not a value any real weapon has.
  double weapon_constant = 1.0;
};

// How two shares of ignored monster DEF meet: in reverse, so what is left of
// the armour is the product of what each share leaves standing. 30% and 40%
// come to 58%, and no pile of sources ever reaches all of it. Every source
// combines this way -- two skills, two items, or a skill and an item.
double CombineIgnoredDefense(double a, double b);

// The first factor of the GMS damage chain, and the game's whole notion of one
// weapon class hitting harder than another. It belongs to the job and the
// weapon together rather than to either alone: a one-handed sword is 1.24 in a
// Paladin's hands and 1.34 in a Hero's.
//
// The published table is keyed by 4th job, which this game does not reach, so
// what is written here is per weapon -- each carrying the constant of the job
// line that owns it -- with an override wherever a line we do have disagrees.
// A weapon no line lists at all is 1.0.
double WeaponConstant(Job job, EquipType weapon);

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
  double crit_dmg = 0.0;   // added critical damage (0.05 == +5%)
  // Weapon mastery, 0..1. 0 means the character has no mastery skill and
  // keeps the beginner's baseline: a mastery skill's first level is worth
  // less than that, and learning a skill must never make a swing worse.
  double mastery = 0.0;
  // Plain % damage and final damage, already combined across every passive
  // that grants them -- summed and multiplied respectively, which is where the
  // two differ. See DerivedStats.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Share of the monster's DEF the passives ignore, already combined across
  // them. Meets the gear's share in reverse, the same way they combined.
  double ied = 0.0;
};

// Builds OffenseStats from a character's job, level and summed stats. The job
// picks primary/secondary; attack, boss_pct and ied come from gear.
//
// `attack_skill` is the learned attack being swung, at `attack_level`, or null
// for the bare 100% poke. Choosing WHICH attack is the caller's job -- it
// depends on how many mobs are up, which this pure per-mob math cannot see.
//
// `weapon` is what is in hand, which the summed EquipStats no longer says: it
// decides the weapon constant, and nothing else here.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped, EquipType weapon,
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
  // Chance the hit misses outright (0.30 == 30%). A miss and a reduction come
  // to the same thing over enough hits, and enough hits is all this function
  // ever reports -- see DerivedStats::dodge_chance.
  double dodge_chance = 0.0;
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
// whole kTickMs units. Stage 1..10, 10 fastest, 4 == base. GMS's own formula.
//
// `base_delay_ms` belongs to the SKILL being swung -- see Skill::base_delay_ms.
// The weapon's say in how fast a character attacks is the stage, and nothing
// else; a spear and a sword swing the same skill at the same speed.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// What the bare poke swings at, and what a skill naming no delay of its own is
// taken to swing at. 780ms is the commonest 1st/2nd job animation, and the one
// both Brandish and Spear Sweep have.
inline constexpr int kDefaultSwingDelayMs = 780;

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
