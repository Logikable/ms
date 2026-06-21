#ifndef MS_SRC_COMBAT_H_
#define MS_SRC_COMBAT_H_

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

// Expected damage of one full attack against a single mob, averaging crit over
// its rate (deterministic; no RNG). Implements the GMS damage chain.
double ExpectedAttackDamage(const OffenseStats& offense, double mob_pdr,
                            bool is_boss);

// Seconds between swings for an attack whose base animation is `base_delay_ms`,
// at the given attack speed stage (1..10, 10 fastest, 4 == base). Modern
// formula: base * (20 - stage) / 16, ceil'd up to 30ms tick boundaries.
double SwingIntervalSeconds(int base_delay_ms, int attack_speed_stage);

// Damage per second of non-stop swinging: expected damage per attack over the
// swing interval.
double Dps(const OffenseStats& offense, double mob_pdr, bool is_boss,
           int base_delay_ms, int attack_speed_stage);

}  // namespace ms

#endif  // MS_SRC_COMBAT_H_
