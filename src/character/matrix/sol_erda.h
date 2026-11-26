/* Sol Erda Energy and Sol Erda Fragments: the two currencies the 6th job's
 * HEXA Matrix is bought with.
 *
 * GMS holds whole Sol Erda (at most 20) beside a gauge of energy that fills
 * one every 1000. Here both are a single uncapped count of energy, priced in
 * Sol Erda by dividing by kEnergyPerSolErda. Fragments have no cap in GMS
 * either.
 *
 * Both drop only from Grandis monsters (maps asking Sacred Power), rolled like
 * any drop and lifted by item drop rate. A character banks them before the 6th
 * advancement, as V Points bank before the 5th. Bosses pay energy per clear.
 *
 * Pure math, like v_matrix.h; the caller adds the amounts to the character.
 */
#ifndef MS_SRC_CHARACTER_SOL_ERDA_H_
#define MS_SRC_CHARACTER_SOL_ERDA_H_

#include <cstdint>
#include <random>

namespace ms {

inline constexpr int64_t kEnergyPerSolErda = 1000;

// The chance a kill drops a Fragment, before item drop rate. Halved
// (2026-11-25) after a sweep finished the whole 6th job by day 190: endgame
// farmers hold +100-180% drop, not the +70% first assumed. Now about day 450.
// GMS's is about 0.044%, but GMS runs ten times faster.
inline constexpr double kFragmentDropChance = 0.0005;

// The chance a kill drops a Faint Sol Erda Energy, and what one holds (GMS's
// 10). Boss clears past 260 pay about 12 Sol Erda per 1,000 Fragments, so
// this makes ~32 against the ~34 a full 6th job spends: both run out together
// (2026-11-25 sweep; the user took 0.1%).
inline constexpr double kEnergyDropChance = 0.001;
inline constexpr int64_t kEnergyPerDrop = 10;

// The average Fragments and energy per kill at `item_drop_pct` (0.20 ==
// +20%), for sims that use the rate instead of rolling.
double FragmentsPerKill(double item_drop_pct);
double SolErdaEnergyPerKill(double item_drop_pct);

// What `kills` actually dropped, in one roll for the whole batch.
int64_t RollMobFragments(int64_t kills, double item_drop_pct,
                         std::mt19937& rng);
int64_t RollMobSolErdaEnergy(int64_t kills, double item_drop_pct,
                             std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SOL_ERDA_H_
