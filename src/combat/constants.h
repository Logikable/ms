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

// What every character crits at before a skill is bought: GMS's 5% chance and
// 35% bonus. Both are SHOWN on the stats page rather than folded away, so a
// skill adding to either adds to a number the player can see.
constexpr double kBaseCritRate = 0.05;
constexpr double kBaseCritDamage = 0.35;

// The three traits at the caps every endgame character fills them to: GMS pays
// Insight in ignored elemental resistance, Ambition in ignored defence and
// Empathy in buff duration. This game has no traits, so what they come to is
// carried as a base, as the crit pair above is.
constexpr double kBaseIgnoreElementalResistance = 0.05;  // Insight
constexpr double kBaseIgnoreDefense = 0.10;              // Ambition
constexpr double kBaseBuffDuration = 0.10;               // Empathy

// How many timed buffs are modelled at once. Every combination needs a damage
// table of its own and the count doubles with each buff, but a table is built
// on first ask and kept in a map, so a raise costs the combinations a fight
// STANDS in rather than every mask. A book over the cap silently loses the
// rest, so //src/data_test:skill_test refuses one. This bounds the PARTY's
// buffs too, which take the bits above the character's own -- and the link
// skills', which every job may equip on top of its own book.
constexpr int kMaxBuffWindows = 15;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
