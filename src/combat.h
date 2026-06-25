#ifndef MS_SRC_COMBAT_H_
#define MS_SRC_COMBAT_H_

#include <cstdint>

#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// GMS global respawn tick: every 7.56s the server refills up to one mob per
// spawn point. A map's full-clear kill cap is spawn_count / this.
constexpr double kRespawnIntervalSeconds = 7.56;

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
// stats. Job selects the primary/secondary stat; attack and the boss_pct/ied
// modifiers come from equipped gear. Remaining modifiers keep their
// placeholder/identity defaults until skills and more gear stats supply them.
OffenseStats OffenseStatsFor(Job job, const AllocatedStats& allocated,
                             const EquipStats& equipped);

// Expected damage of one full attack against the given mob, averaging crit over
// its rate (deterministic; no RNG). Implements the GMS damage chain, drawing
// the mob's PDR and boss flag from the Mob.
double ExpectedAttackDamage(const OffenseStats& offense, const Mob& mob);

// Seconds between swings for an attack whose base animation is `base_delay_ms`,
// at the given attack speed stage (1..10, 10 fastest, 4 == base). Modern
// formula: base * (20 - stage) / 16, ceil'd up to 30ms tick boundaries.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Base basic-attack swing animation, in milliseconds, for a weapon of the given
// type (the stage-4 / x1.0 reference that SwingIntervalSeconds scales). Base
// animation is a property of the weapon class, not the individual item;
// per-item speed is the attack_speed stage instead.
int BaseAttackDelayMs(EquipType equip_type);

// Mobs killed per second of non-stop farming on `map` against `mob`: the lower
// of the DPS-limited clear rate and the map's respawn cap (spawn_count over the
// respawn tick), slowed by the global game speed factor so both regimes stretch
// equally. Damage is dealt in discrete swings, so each mob takes
// ceil(mob.max_hp / damage_per_hit) hits and overkill on the last hit is
// wasted — damage never carries over to the next mob. damage_per_hit and
// swing_interval_seconds come from ExpectedAttackDamage and
// SwingIntervalSeconds. respawn_interval_seconds defaults to the real GMS tick;
// override it in tests to pick round spawn caps.
double KillsPerSecond(
    double damage_per_hit, double swing_interval_seconds, const Mob& mob,
    int max_targets, const MapData& map,
    double respawn_interval_seconds = kRespawnIntervalSeconds);

// EXP earned per second at the given kill rate.
double ExpPerSecond(double kills_per_second, int64_t mob_exp);

}  // namespace ms

#endif  // MS_SRC_COMBAT_H_
