/* Constants that several parts of combat must agree on. A value used by only
 * one file belongs in that file's anonymous namespace.
 */
#ifndef MS_SRC_COMBAT_CONSTANTS_H_
#define MS_SRC_COMBAT_CONSTANTS_H_

namespace ms {

// GMS's global respawn timer: every 7.56s, each spawn point refills with up to
// one mob. A map's kill rate is capped at its spawn points divided by this.
constexpr double kRespawnIntervalSeconds = 7.56;

// The game-speed setting (how much slower than GMS the game runs) moved to
// GameSpeedFactor(level) in src/character/progression.h, since it now scales
// with level.

// GMS rounds attack delays up to a multiple of this. It is not a simulation
// tick; nothing steps by it.
constexpr int kTickMs = 30;

// Base crit chance and crit damage for every character, from GMS: 5% and 35%.
// Both show on the stats page, so skills that raise them change a visible
// number.
constexpr double kBaseCritRate = 0.05;
constexpr double kBaseCritDamage = 0.35;

// The three traits at the maximum every endgame character reaches. In GMS,
// Insight gives ignore elemental resistance, Ambition gives ignore defense, and
// Empathy gives buff duration. This game has no traits, so these are base
// values, like the crit pair above.
constexpr double kBaseIgnoreElementalResistance = 0.05;  // Insight
constexpr double kBaseIgnoreDefense = 0.10;              // Ambition
constexpr double kBaseBuffDuration = 0.10;               // Empathy

// How many timed buffs are modeled at once. Each combination of active buffs
// needs its own damage table, so the count doubles per buff. Tables are built
// on demand and cached, so only combinations a fight actually reaches cost
// anything. Buffs past the cap are silently dropped, so
// //src/data_test:skill_test rejects any job book over it. Party buffs and link
// skills count toward the cap too.
constexpr int kMaxBuffWindows = 15;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
