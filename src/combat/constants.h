/* Constants shared across the combat module. Anything used by only one file in
 * here belongs in that file's anonymous namespace instead -- this header is for
 * the handful of values several parts of combat must agree on.
 */
#ifndef MS_SRC_COMBAT_CONSTANTS_H_
#define MS_SRC_COMBAT_CONSTANTS_H_

namespace ms {

// GMS global respawn tick: every 7.56s the server refills up to one mob per
// spawn point. A map's full-clear kill cap is its spawn points over this.
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

// What every character ignores of a boss's elemental resistance before a
// single skill is bought. GMS's one universal source is the Insight trait,
// worth half a point per ten levels of it and capped at 5%, which every
// endgame character has filled; this game has no traits, so the cap is carried
// as a base the way the crit pair above is.
constexpr double kBaseIgnoreElementalResistance = 0.05;

// How many timed buffs are modelled at once. Every combination of them needs a
// damage table of its own, and the count of combinations doubles with each
// one -- but a table is built the first time the fight asks for it, so what a
// raise really costs is the combinations a fight stands in rather than every
// one it could. A character holding more than this keeps the first of them and
// silently loses the rest, so //src/data_test:skill_test refuses a book that
// hands out more.
//
// This bounds the PARTY'S buffs too, which take the bits above the
// character's own: a book at the cap standing beside two allies casting one
// each is what the last two are for. See BuffedSetSource::ally_buffs.
constexpr int kMaxBuffWindows = 11;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
