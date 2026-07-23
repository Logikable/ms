/* How hard, and how often, the player hits: the GMS damage formula and the
 * attack-speed timing that feeds it. Pure math over a character's stats and a
 * mob -- no game state, no notion of a fight in progress.
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
// job, not this pure per-mob math; today there is at most one attack skill.
OffenseStats OffenseStatsFor(Job job, int level,
                             const AllocatedStats& allocated,
                             const EquipStats& equipped,
                             const Skill* attack_skill, int attack_level);

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

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
