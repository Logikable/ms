/* Values a cube on one worn piece the way the shopper values a scroll or a
 * star: expected combat power for the meso it costs.
 *
 * There is no goal or order of pieces here. A player who cubes their weapon
 * before their belt does so because the weapon's lines are worth more. That is
 * a comparison, not a rule, so this computes what one cube is expected to add
 * and lets GearShopper rank it against everything else.
 *
 * The value is marginal and keep-better: a cube is worth how much the reroll
 * beats the item's current lines, averaged over draws, and never less than
 * zero. That values the rank ladder correctly even though it looks like it
 * needs a plan, because the ladder is geometric: each cube on the way to
 * Legendary is worth the same. It undervalues goals that need two specific
 * lines, such as the -3s hat and the meso-and-drop accessory; both are meant to
 * lose.
 *
 * A %meso or %drop line earns income rather than power, so it is valued over
 * the rest of the run instead (see CubeIncome).
 */
#ifndef MS_ANALYSIS_CUBE_PLAN_H_
#define MS_ANALYSIS_CUBE_PLAN_H_

#include <cstdint>
#include <functional>
#include <random>

#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Draws used to value one cube. Enough that a good line isn't missed through a
// slot's worth of bad luck, few enough to keep a look cheap.
inline constexpr int kCubeSamples = 64;

// Weight given to an item the shopper may replace later. The user's call:
// cubing gear you'll outgrow is still worth something, since it carries you to
// the replacement, but less than cubing a piece you'll keep.
inline constexpr int kReplaceableNumerator = 1;
inline constexpr int kReplaceableDenominator = 4;

// Time left in the run and the current income, which together decide what a
// %meso or %drop line is worth.
struct CubeIncome {
  double seconds_left = 0.0;
  // Meso per second the character would earn at `meso_bonus` (MesoBonus's
  // result, both caps applied) and `drop_pct`. Empty for a caller with no
  // encounter, which values income lines at zero.
  std::function<double(double meso_bonus, double drop_pct)> rate;
  // Combat power a meso buys elsewhere on the shelf. Only the ranking depends
  // on this: a line pays for itself when its income over the horizon beats the
  // cube's price, which needs no rate. So a stale value can misrank but never
  // misdecide.
  double power_per_meso = 0.0;
};

// The character as they are now, which every cube is valued against. Computing
// it needs a rebuild, so the shopper does it once per round.
struct CubeBasis {
  DerivedStats derived;
  // Stats from everything worn plus everything granted, before percentages are
  // applied: the sum TotalEquipStats folds, not its result. A potential can
  // change %ATT, so the fold is redone per candidate.
  EquipStats raw;
  // The fight lines are judged against. An ignored-defence line's value depends
  // on the fight, not the character, and changes threefold between Cygnus and
  // Lotus, so the monster itself is carried. See //analysis:yardstick.
  Yardstick yard;
};

CubeBasis CubeBasisFor(const GameState& state, const Yardstick& yard);

// A run of cubes on one slot, and what it's expected to leave. It's priced as a
// run rather than a single cube because a character below a boss's defence wall
// gains nothing from any one roll but has a real chance over dozens. Priced one
// at a time, the slot that most needs cubing would never get one.
struct CubeProgram {
  int cubes = 0;      // cubes in the run
  double gain = 0.0;  // expected gain of the run's best roll
  int64_t cost = 0;   // total cost of the run

  bool worth() const {
    return cubes > 0 && gain > 0.0 && cost > 0;
  }
};

// The run on `slot` with the best value per meso, out of a ladder of lengths.
// The whole ladder comes from one sample: a run of N leaves the best of N
// draws, and the chance that the best of N is the i-th of a sorted sample of m
// is (i/m)^N - ((i-1)/m)^N. So a long run costs no more to price than one cube.
CubeProgram BestCubeProgram(const GameState& state, const CubeBasis& basis,
                            EquipSlot slot, const CubeIncome& income,
                            std::mt19937& rng);

// Whether `rolled` beats what `slot` already has: the same comparison
// BestCubeProgram averages, applied to one actual roll. The cube is paid for
// either way, so this only decides which lines the item keeps.
bool WorthTaking(const GameState& state, const CubeBasis& basis, EquipSlot slot,
                 const Potential& rolled, const CubeIncome& income);

// Whether the shopper is likely to replace what `slot` holds: a higher-level
// piece the character can already wear and afford. A weapon must match the type
// in hand, since a Lv140 sword doesn't replace a Lv120 axe.
//
// Listed is not reachable: a tier priced in tokens counts only once a token is
// in the bag, so a character locked out of the fight that drops them cubes what
// they have. The gain is discounted rather than refused, because meso spent
// cubing still helps the climb toward the replacement.
bool Replaceable(const GameState& state, EquipSlot slot);

}  // namespace ms

#endif  // MS_ANALYSIS_CUBE_PLAN_H_
