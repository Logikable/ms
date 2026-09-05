/* Constants shared across the combat module. Anything used by only one file in
 * here belongs in that file's anonymous namespace instead -- this header is for
 * the handful of values several parts of combat must agree on.
 */
#ifndef MS_SRC_COMBAT_CONSTANTS_H_
#define MS_SRC_COMBAT_CONSTANTS_H_

namespace ms {

// GMS global respawn tick: every 7.56s the server refills up to one mob per
// spawn point. A map's full-clear kill cap is spawn_count / this.
constexpr double kRespawnIntervalSeconds = 7.56;

// The pacing knob that used to live here -- how many times slower than GMS the
// game runs -- is now GameSpeedFactor(level) in src/character/progression.h. It
// stopped being a constant when it started stretching with the player's level.

// The action-delay quantization grain: GMS rounds attack delays up to whole
// units of this. Not a simulation tick -- nothing here is stepped by it.
constexpr int kTickMs = 30;

// What every character crits at before a single skill is bought: GMS gives
// them a 5% chance and a 35% bonus when it lands. Both are shown on the stats
// page rather than folded away, so a skill adding to either reads as adding to
// a number the player can already see.
constexpr double kBaseCritRate = 0.05;
constexpr double kBaseCritDamage = 0.35;

// How many timed buffs are modelled at once. Every combination of them needs a
// damage table of its own, so the count of tables doubles with each one --
// which is affordable at four and would stop being so before long. A character
// holding more keeps the first four and silently loses the rest, so
// //src/data_test:skill_test refuses a book that hands out more.
//
// Five of the ten jobs sit exactly on it -- Hero, Paladin, Dark Knight, Bow
// Master and Bishop -- so the next buff written for any of them has to raise
// this first.
constexpr int kMaxBuffWindows = 4;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
