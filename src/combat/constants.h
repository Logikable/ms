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

// How many timed buffs are modeled at once: one bit each of an int mask, so 30
// is as far as it goes. Tables are built per combination a fight reaches, not
// per possible one, so the count costs nothing until buffs are raised. Buffs
// past it are silently dropped, so //src/data_test:skill_test rejects a book
// over it; party buffs take what the character's own leave.
constexpr int kMaxBuffWindows = 30;

// Uses of an Ascent skill a boss fight allows, phases included (client
// 6thCount). The run releases them as the boss's HP falls; see BossRun.
constexpr int kAscentUsesPerBossFight = 3;

// A buff on at least this cooldown is a burst window. A released Ascent waits
// for every one to lapse, since buffs don't raise it and the cast costs the
// buffed seconds it replaces.
constexpr double kBurstCooldownSeconds = 90.0;

// A boss phase one swing can end within this many seconds is finished rather
// than buffed for: past it, raising buffs first ends it sooner. See
// CombatSim::Finisher.
constexpr double kFinishHorizonSeconds = 1.0;
// How much later than the soonest a finishing swing may end the phase and
// still be chosen for a shorter cooldown, as a share of the soonest.
constexpr double kFinishSlack = 0.10;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
