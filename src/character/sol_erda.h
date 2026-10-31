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

// The chance a kill drops a Fragment, before item drop rate. Paced so the whole
// matrix and three HEXA Stat nodes (about 52,500) take about a year at 160,000
// kills a day with no drop rate, and half a year at +70%. GMS's is about
// 0.044%, but GMS runs ten times faster.
inline constexpr double kFragmentDropChance = 0.001;

// The chance a kill drops a Faint Sol Erda Energy, and what one holds (GMS's
// 10). Paced to fill the matrix's 1,702 Sol Erda in about four fifths of the
// Fragments' time, so Fragments are what a player waits on, as in GMS.
inline constexpr double kEnergyDropChance = 0.004;
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
