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

// Builds OffenseStats from a character's job, level, and summed (allocated +
// equipped) stats. Job picks primary/secondary; attack and boss_pct/ied come
// from gear; level feeds the level multiplier; the rest keep identity defaults
// until skills/gear supply them.
//
// `attack_skill` is the attack the character swings with -- an attack-kind
// Skill they have learned, at `attack_level` -- or nullptr to fall back to the
// bare 100% poke. Its skill_pct (base + per_level*(attack_level-1)) becomes the
// swing's multiplier. Choosing WHICH attack (when several are learned, or when
// a weaker multi-target skill beats the poke on a crowded map) is the caller's
// job, not this pure per-mob math.
//
// `passives` is what the character's learned passives add on top -- see
// DerivedStatsFor, which walks them. It arrives already resolved rather than
// as a skill list because it applies to every swing, whichever attack was
// chosen.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped,
                             const Skill* attack_skill, int attack_level,
                             const PassiveOffense& passives = {});

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// A single number for "how hard this character hits", for comparing characters
// rather than predicting a swing: the damage chain with everything that depends
// on the target, the job, or the moment stripped out. Only `primary`,
// `secondary`, `attack`, `mastery`, `damage_pct`, `boss_pct`, `crit_rate`,
// `crit_dmg` and `final_dmg_pct` are read -- skill_pct, lines, ied, ier and
// level are ignored, so build the stats with a null attack skill.
//
// Two deliberate departures from GMS, which computes this off a maximum,
// boss-facing hit. Boss damage counts unconditionally, as it does there. But
// crit is weighted by its rate -- GMS's flat `1.35 + crit damage` prices a
// point of critical damage the same whether it lands every swing or never,
// which our own damage chain does not.
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

// Expected damage of one hit from `mob` -- its minimum and maximum rolls
// averaged, no RNG. Never less than 1, as in GMS: a mob whose whole attack the
// character's DEF has cancelled still takes a point off them per hit.
//
// DEF subtracts flatly from the mob's attack, but only up to a cap: it can
// never cancel more than 80% of the attack on a maximum roll, so even an
// absurdly armoured character still takes about a fifth of what the mob swings
// for. That cap is the whole shape of the thing. On a map near the character's
// own level their DEF clears it easily and more armour buys nothing; on a map
// far above it DEF is the small number, the cap never binds, and the mob's
// attack lands close to full.
double ExpectedDamageTaken(const DefenseStats& defense, const Mob& mob);

// Damage multiplier from the level gap between attacker and monster (the GMS
// "level multiplier", always applied): a small bonus at or above the monster's
// level -- 1.1 at equal, rising to 1.2 at +5 and beyond -- and a growing
// penalty below it, reaching 0 at 40 levels under, where the game floors output
// to 1 damage.
double LevelMultiplier(int player_level, int mob_level);

// Seconds between swings: base_delay_ms * (20 - stage) / 16, rounded up to
// whole kTickMs units. Stage 1..10, 10 fastest, 4 == base.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Base swing animation (ms) for a weapon type -- the stage-4 reference scaled
// by SwingIntervalSeconds. A weapon-class property, not per-item.
int BaseAttackDelayMs(EquipType equip_type);

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
