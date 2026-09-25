/* How hard and how often the player hits, and how hard a mob hits back: the GMS
 * damage formulas and attack-speed timing. Pure math over stats and a mob, with
 * no game state or fight in progress.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_H_
#define MS_SRC_COMBAT_DAMAGE_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Inputs to the GMS damage formula. Every modifier defaults to its neutral
// value.
struct OffenseStats {
  int primary = 0;    // total primary stat
  int secondary = 0;  // total secondary stat
  int attack = 0;     // total weapon/gear attack
  int level = 0;      // attacker level, for the level multiplier
  // The minimum of each line's roll: it raises min damage and leaves max alone.
  // 1.0 never rolls low but doesn't hit harder. See BaseMastery.
  double mastery = 0.20;
  double skill_pct = 1.0;  // skill damage multiplier (1.0 == 100%)
  int lines = 1;           // hits per attack
  // Shadow copies of the attack's hits, kept separate from `lines`: usually
  // equal to `lines`, each dealing `mirror_pct` of a real hit. Separate so the
  // UI can tell real hits from copies.
  int mirror_lines = 0;
  double mirror_pct = 0.0;
  double damage_pct = 0.0;  // additive %dmg, as fraction
  double boss_pct = 0.0;    // additive boss %dmg; applies only vs bosses
  // Like boss_pct, but against non-bosses. It's the character's own and adds
  // into the same sum as damage_pct.
  double normal_pct = 0.0;
  // Added to skill_pct against non-bosses, so it applies once per line. Comes
  // from the attack, not the character.
  double normal_skill_pct = 0.0;
  // The formula adds these to every character's base values in constants.h;
  // these hold only what gear and skills add.
  double crit_rate = 0.0;      // 0..1
  double crit_dmg = 0.0;       // 0..1
  double final_dmg_pct = 0.0;  // final damage, as fraction
  double ied = 0.0;            // ignore enemy defense, 0..1
  // Ignored boss elemental resistance, above the base in constants.h.
  // Resistance halves boss damage, so 1.0 means no halving at all.
  double ier = 0.0;
  // GMS's weapon constant; see WeaponConstant. 1.0 is a neutral default, not a
  // real weapon's value.
  double weapon_constant = 1.0;
  // Multiplier from the map's force requirement (Arcane Force or Sacred Power).
  // Applied last, after the level multiplier, so it scales the whole hit.
  double force_pct = 1.0;
};

// What varies from one hit to the next. Expected-value math uses the averages;
// a live attack rolls these per line and per enemy.
struct SwingRolls {
  int lines = 1;         // the attack's own hits on one enemy
  int mirror_lines = 0;  // shadow copies, each worth mirror_pct of a real hit
  double mirror_pct = 0.0;
  // The minimum of each line's roll. 1.0 never rolls low, so the default means
  // no variance.
  double mastery = 1.0;
  // Unlike the OffenseStats fields, these already include the base values every
  // character has, since the roll uses the total chance.
  double crit_rate = 0.0;
  double crit_dmg = 0.0;
};

// The random parts of `offense`. Everything else is fixed before the hit.
SwingRolls RollsFor(const OffenseStats& offense);

// One damage line: its share of the attack's expected damage, and whether it
// crit. For callers that draw each line's number.
struct LineRoll {
  double share = 0.0;
  bool crit = false;
};

// One hit's random multiplier on the expected damage: the per-line rolls summed
// and divided by their average, so exactly 1.0 when nothing is random. Returned
// as a factor because the expected damage is already cached.
//
// If `lines` is given, it is filled with each line's share. The shares sum to
// the return value, so the numbers shown add up to the HP lost.
double RollFactor(const SwingRolls& rolls, std::mt19937& rng,
                  std::vector<LineRoll>* lines = nullptr);

// Combines two ignore-defense values multiplicatively on the remaining defense:
// 30% and 40% make 58%, and stacking never reaches 100%. All sources combine
// this way.
double CombineIgnoredDefense(double a, double b);

// The first factor of the GMS damage formula, which makes some weapon types hit
// harder than others. It depends on job and weapon: a one-handed sword is 1.24
// for a Paladin and 1.34 for a Hero. Each weapon lists its main job line's
// constant, with overrides where another line differs; unlisted weapons are
// 1.0.
double WeaponConstant(Job job, EquipType weapon);

// A job line's mastery before any mastery skill: 20% melee, 15% bow and
// crossbow, 25% wand and staff. Mastery skills add to this (70/65/75 in 2nd
// job, 90/85/95 in 4th), so a mastery skill can never lower it.
double BaseMastery(Job job);

// Whether skills of this kind have their own damage multiplier: attacks, and
// skills that fire on their own timer.
bool DealsDamage(SkillKind kind);

// Every field at `level`: base + per_level * (level - 1), over the whole
// message. Use this rather than computing fields one by one; it covers every
// field, so new ones need no changes here. Bools have no per-level value, so
// one set on either side stays set.
SkillEffect EffectAt(const SkillEffect& base, const SkillEffect& per_level,
                     int level);

// A buff's party portion after scaling with the caster's INT. Each
// ally_int_lever adds its effect per full `caster_int` step, capped at the
// ceiling it names or else at the caster's own value divided by `party_size`.
//
// `half` is the party portion at the caster's level, `own` is the caster's, and
// `party_size` includes the caster. Rounded to whole percentage points, as GMS
// states them. See AllyIntLever.
SkillEffect GrownByCasterInt(const Buff& buff, const SkillEffect& half,
                             const SkillEffect& own, int caster_int,
                             int party_size);

// A per-level value as a whole number. SkillEffect values are doubles so that
// something rising every few levels can be a fractional step: GMS's ceil(L/5)
// is `base 1, per_level 0.2`. Floored, with an epsilon because 1 + 0.2 * 5 is
// not exactly 2 in floating point.
int WholeValue(double value);

// Hits per enemy at `level`: `lines` plus whole lines gained from
// `lines_per_level` since level 1, minimum 1. All code reads lines through
// here, so they rise everywhere at once.
int SkillLinesAt(const Skill& skill, int level);

// Number of strikes one attack makes, each of SkillLinesAt lines. Doesn't vary
// by level; GMS gives a fixed slash count. See Skill::casts.
int SkillCasts(const Skill& skill);

// Number of strikes an extra hit makes, same as SkillCasts. See
// SwingHit::casts.
int SwingHitCasts(const SwingHit& hit);

// An empowered skill's name: "Empowered " plus the target's name. Boosts are
// keyed by it, so the fight and the stat code must build it the same way.
std::string EmpoweredSkillName(const std::string& target);

// The skill names `boost` applies to: its target, its empowered form, or both,
// per BoostReach. All code goes through here so the fight, stats and skill page
// agree.
std::vector<std::string> BoostTargetNames(const SkillBoost& boost);

// Combo Orbs at `level`: `combo_orbs` plus whole orbs gained since level 1. In
// one place so the stats and skill page always agree.
int ComboOrbsAt(const Skill& skill, int level);

// Cooldown at `level`, in GMS seconds: `cooldown_seconds` plus the per-level
// step. In one place for the same reason as SkillLinesAt.
double CooldownAt(const Skill& skill, int level);

// Whether `pulse` deals damage at all: on its own timer, or on the attacks of
// the skill it's attached to. The latter has no interval, so checking
// cast_interval_seconds isn't enough.
bool Pulses(const BuffPulse& pulse);

// The longest `buff` can last at level 1: its own duration, or its longest
// form. Buffs with forms have no duration of their own, so use this rather than
// duration_seconds.
double LongestBuffDuration(const Buff& buff);

// A cooldown `wait` after a potential's cooldown reduction. GMS's rule, not
// simple subtraction: cooldowns under 5 seconds aren't reduced, 5 to 10 lose 5%
// of themselves per second of reduction, and for longer ones the part that
// would fall below 10 seconds is halved.
double ReducedCooldown(double wait, double reduction_seconds);

// Hits `shield` blocks at `level`, floored. In one place because the fight and
// the skill page both use it.
int ShieldHitsAt(const Shield& shield, int level);

// What the job book gives one skill by name, summed over every skill that
// grants it. Only applied to that skill's own attacks, never to the character's
// other attacks. See SkillBoost::effect.
struct SkillBonus {
  // Percentage points added to the named skill, applied once per line. If the
  // target is a passive with a Final Attack, it goes to that hit's multiplier.
  double skill_pct = 0.0;
  // % damage for this skill only, added to what gear and passives give. This is
  // what Hyper Skill Reinforce grants.
  double damage_pct = 0.0;
  // Boss damage, added to the target's and the character's; all three are
  // shares of the same damage.
  double boss_pct = 0.0;
  // The same against non-bosses.
  double normal_pct = 0.0;
  // Ignore defense, combined multiplicatively with the target's own rather than
  // added: two 20% sources leave 64% of DEF, not 60%.
  double ied = 0.0;
  double crit_rate = 0.0;
  // Final damage, multiplied with the target's own.
  double final_dmg_pct = 0.0;
  // Chance added to the named skill's Final Attack. It applies to the extra
  // hit, so it is added to that source rather than to the attack.
  double final_attack_chance = 0.0;
  // Multiplier on that chance after all additive sources.
  double final_attack_chance_mult = 1.0;
  // Percentage points on the named skill's burn. The burn has its own
  // multiplier, so this is added in BurnFor rather than to the attack.
  double dot_skill_pct = 0.0;
  // Seconds added to the named skill's burn, which has its own timer. See
  // SkillBoost::dot_duration_seconds.
  double dot_duration_seconds = 0.0;
  // Extra hits added to the named skill's attack, already evaluated at the
  // granting skill's level, so each has its full damage in `base` and no
  // per-level value. See SkillBoost::extra_hit.
  std::vector<SwingHit> extra_hit;
  // A wound the named skill applies, given by the skill that describes it: for
  // example, Trickblade adds a wound to Assassinate and Sonic Blow. See Wound.
  int wound_stacks = 0;
  int wound_max_stacks = 0;
  double wound_seconds = 0.0;
};

// What a character's passives add to every attack, whichever one they use. None
// of it depends on the target. See character_stats.h.
struct PassiveOffense {
  double crit_rate = 0.0;  // added crit chance (0.40 means 40%)
  double crit_dmg = 0.0;   // added crit damage (0.05 means +5%)
  // Mastery from the best mastery skill, 0 to 1, before adding the job line's
  // base. 0 without one.
  double mastery = 0.0;
  // Already combined across all passives: damage_pct by adding, final_dmg_pct
  // by multiplying.
  double damage_pct = 0.0;
  double final_dmg_pct = 0.0;
  // Fraction of a line a shadow copy of the attack deals, per line the attack
  // has. See DerivedStats::mirror_line_pct.
  double mirror_line_pct = 0.0;
  // Extra hits added to attacks that already hit more than once. See
  // SkillEffect::bonus_attack_lines.
  int bonus_attack_lines = 0;
  // Boss damage. Adds to gear's boss damage, since both are shares of the same
  // damage (unlike ied).
  double boss_pct = 0.0;
  // The same against non-bosses, from Hyper Stats, Inner Ability and passives.
  // No gear has it.
  double normal_pct = 0.0;
  // Ignore defense from passives, already combined. Combined multiplicatively
  // with gear's.
  double ied = 0.0;
  // Ignore elemental resistance, summed across passives. No gear has it, so
  // this is the character's total. See OffenseStats::ier.
  double ier = 0.0;
  // Bonuses to specific skills from the job book, keyed by display name. Only
  // the attacking skill's entry is used.
  std::map<std::string, SkillBonus> skill_bonus;
  // Multiplier from the map's force requirement: 1 on maps with none, down to
  // 5% when short of Sacred Power, and up to 1.5 with enough excess Arcane
  // Force.
  double force_pct = 1.0;
};

// Builds OffenseStats from a character's job, level and stats. `attack_skill`
// is the attack used, at `attack_level`, or null for a basic attack. The caller
// picks which attack, since that depends on the crowd. `weapon` only sets the
// weapon constant.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped, EquipType weapon,
                             const Skill* attack_skill, int attack_level,
                             const PassiveOffense& passives = {});

// Expected damage of one full attack against `mob`, with crit averaged over its
// chance. The GMS damage formula; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// How much one more point of ignore defense is worth, as a fraction of current
// damage, for a character already ignoring `ied`. With remaining defense c,
// ignoring a further e gains e * c/(1 - c); this returns c/(1 - c).
//
// 0 against a monster with no defense or defense already fully ignored. Also 0
// when c is 1 or more (defense over 100%): damage is at its 1-damage floor and
// more ignore defense doesn't help.
double DefenseShare(const Mob& mob, double ied);

// A single "how hard this character hits" number, for comparing characters
// rather than predicting an attack. The damage formula with everything that
// depends on the target, skill or timing removed. Build the stats with a null
// attack skill.
//
// Unlike GMS, crit damage is weighted by crit chance, so it's worth more when
// crits happen more. `vs_boss` picks boss or normal % damage; never both.
int CombatPower(const OffenseStats& offense, bool vs_boss);

// A character's defensive stats, the counterpart to OffenseStats. Level affects
// how much DEF counts.
struct DefenseStats {
  int level = 0;
  int def = 0;
  // Fraction of incoming damage removed after the formula (0.10 means 10%
  // less).
  double damage_taken_pct = 0.0;
  // Chance to dodge a hit. Over many hits this equals a damage reduction, which
  // is all this reports.
  double dodge_chance = 0.0;
  // Reduction to the monster's attack before the formula, and whether it
  // applies to bosses. Separate from damage_taken_pct because it works
  // differently: DEF cancels more of a weakened monster's attack.
  double enemy_attack_pct = 0.0;
  bool enemy_attack_reaches_boss = false;
  // Multiplier from the map's force requirement: 1 on maps with none, up to 2.8
  // with no Arcane Force, and 0 with enough excess, which the damage floor
  // turns into GMS's 1 damage.
  double force_taken = 1.0;
};

// Expected damage of one hit from `mob`: average of min and max, minimum 1 as
// in GMS. DEF subtracts a flat amount but cancels at most 80%. Near the
// character's level, DEF hits the cap and more is useless; far above it, the
// cap never applies.
double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob);

// The GMS level multiplier: 1.1 at the monster's level, rising to 1.2 at +5 and
// above, and falling to 0 at 40 levels below.
double LevelMultiplier(int player_level, int mob_level);

// GMS's formula: base_delay_ms * (20 - stage) / 16, rounded up to whole kTickMs
// units. Stage runs 1 to 10, 10 fastest, 4 is neutral. `base_delay_ms` belongs
// to the skill; the weapon only sets the stage, so a spear and a sword use a
// skill at the same speed.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// The stage where the formula above leaves the delay unchanged, for skills that
// ignore attack speed. Named because a bare 4 would read as "average weapon"
// when it means "no scaling".
inline constexpr int kUnscaledAttackSpeedStage = 4;

// The fastest stage most characters reach. A soft cap: sources allowed to
// exceed it add on top; see AttackSpeedStage.
inline constexpr int kAttackSpeedSoftCap = ATTACK_SPEED_FASTEST_1;

// `base` plus `bonus`, capped at the soft cap, then plus `uncapped`. Only
// `uncapped` can exceed the cap, which is why it's still worth a stage to a
// character already at 8.
int AttackSpeedStage(int base, int bonus, int uncapped);

// True for jobs that attack with magic attack. The formula is the same; only
// the stat read and the weapon's effect on attack speed differ.
bool SwingsOnMagic(Job job);

// The attack speed stage before passives: the weapon's own, except GMS casts
// all spells at the neutral stage. A staff is Slow, but a mage casting with one
// isn't.
int BaseAttackSpeedStage(Job job, int weapon_stage);

// Delay for a basic attack, and for skills that don't list one. 780ms is the
// most common 1st/2nd job animation.
inline constexpr int kDefaultSwingDelayMs = 780;

// Cast time for skills whose animation never plays. GMS spaces skill sequences
// at a flat 120ms and skips the cast animation for attacks usable mid-attack.
// Not affected by attack speed, since there's no animation to shorten.
inline constexpr int kWaivedCastMs = 120;

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
