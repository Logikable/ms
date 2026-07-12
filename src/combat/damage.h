/* How hard, and how often, the player hits: the GMS damage formula and the
 * attack-speed timing that feeds it. Pure math over a character's stats and a
 * mob -- no game state, no notion of a fight in progress.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_H_
#define MS_SRC_COMBAT_DAMAGE_H_

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// Offensive parameters feeding the GMS damage formula. Modifier fields default
// to their identity (no-effect) value; real values graduate in one at a time as
// gear, skills, etc. produce them.
struct OffenseStats {
  int primary = 0;             // total primary stat
  int secondary = 0;           // total secondary stat
  int attack = 0;              // total weapon/gear attack
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

// Builds OffenseStats from a character's job and summed (allocated + equipped)
// stats. Job picks primary/secondary; attack and boss_pct/ied come from gear;
// the rest keep identity defaults until skills/gear supply them.
OffenseStats OffenseStatsFor(Job job, const AllocatedStats& allocated,
                             const EquipStats& equipped);

// Expected damage of one full attack against `mob` (crit averaged over its
// rate, no RNG). The GMS damage chain; mob PDR and boss flag come from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// Seconds between swings: base_delay_ms * (20 - stage) / 16, rounded up to
// whole kTickMs units. Stage 1..10, 10 fastest, 4 == base.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Base swing animation (ms) for a weapon type -- the stage-4 reference scaled
// by SwingIntervalSeconds. A weapon-class property, not per-item.
int BaseAttackDelayMs(EquipType equip_type);

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_H_
