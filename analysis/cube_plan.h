/* What a cube into one worn piece is worth, priced the way the shopper prices
 * a scroll or a star: expected combat power for the meso it takes.
 *
 * There is no goal here and no order of pieces. A player who cubes their
 * weapon before their belt does it because the weapon's lines are worth more,
 * and that is a comparison rather than a rule -- so this answers what one cube
 * is expected to add and lets GearShopper rank it against everything else on
 * the shelf.
 *
 * The value is marginal and keep-better: a cube is worth what the reroll beats
 * the item's own lines by, averaged over draws, and never less than nothing.
 * That prices the rank ladder correctly for all that it looks like it needs a
 * plan -- one cube at Unique is worth 2.4% of what Legendary adds, and the
 * forty-two-cube climb to Legendary is worth the same per cube, because the
 * ladder is geometric. Where it undervalues is a goal naming two particular
 * lines, which is the -3s hat and the meso-and-drop accessory; both are meant
 * to lose.
 *
 * A %meso or %drop line pays income rather than power, so it is weighed over
 * what is left of the run instead -- see CubeIncome.
 */
#ifndef MS_ANALYSIS_CUBE_PLAN_H_
#define MS_ANALYSIS_CUBE_PLAN_H_

#include <functional>
#include <random>

#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Draws taken to price one cube. Enough that a line worth having is not missed
// by a slot's worth of unlucky rolls, few enough that a look stays cheap.
inline constexpr int kCubeSamples = 64;

// What is left of an item the shopper may yet replace, as a share. The user's
// call: cubing gear you will outgrow is still worth something, since it is
// what carries you to the gear that replaces it, but it is not worth what
// cubing a piece you will keep is worth.
inline constexpr int kReplaceableNumerator = 1;
inline constexpr int kReplaceableDenominator = 4;

// The share of a boss's defence a swing meets, for pricing an ignored-defence
// line. CombatPower leaves ignored defence out on purpose -- it is a fact
// about the target rather than the character -- but it is one of the three
// lines a weapon is cubed for, so a shopper blind to it would never buy one.
// Bosses in the catalog carry 40% to 100%; this is the middle of them.
inline constexpr double kBossPdr = 0.50;

// What the run still has ahead of it and what it is earning, which is the
// whole of what a %meso or %drop line is worth.
struct CubeIncome {
  double seconds_left = 0.0;
  // Meso a second the character would earn at `meso_bonus` (MesoBonus's own
  // answer, both caps already taken) and `drop_pct`. Empty for a caller with
  // no encounter in hand, which values the income lines at nothing.
  std::function<double(double meso_bonus, double drop_pct)> rate;
  // What a meso buys in combat power elsewhere on the shelf. Only the ORDER
  // depends on this: a line pays for itself when its income over the horizon
  // beats the cube's price, and that test needs no rate at all. So a stale one
  // misranks and never misdecides.
  double power_per_meso = 0.0;
};

// The character as they stand, which every cube is priced against. Working one
// out costs a rebuild, so the shopper takes it once a round.
struct CubeBasis {
  DerivedStats derived;
  // Everything worn plus everything granted, before any percentage is folded
  // in -- the sum TotalEquipStats folds, not its answer. A potential moves
  // %ATT, so the fold has to be redone per candidate.
  EquipStats raw;
};

CubeBasis CubeBasisFor(const GameState& state);

// What one cube into `slot` is expected to add, in the combat power the rest
// of the shelf is priced in. Zero where the slot is empty, takes no potential,
// or holds nothing a cube could improve.
int CubeGain(const GameState& state, const CubeBasis& basis, EquipSlot slot,
             const CubeIncome& income, std::mt19937& rng);

// Whether `rolled` beats what `slot` already holds -- the same comparison
// CubeGain averages over, asked once of a roll in hand. A cube is paid for
// either way, so this decides only what the item ends up wearing.
bool WorthTaking(const GameState& state, const CubeBasis& basis, EquipSlot slot,
                 const Potential& rolled, const CubeIncome& income);

// Whether the shopper is likely to replace what `slot` holds: the catalog
// offers a piece for that slot at a higher level which the character can
// already wear. A weapon has to match the type in hand as well -- a Lv140
// sword is not a replacement for a Lv120 axe a Hero measured their way into,
// and counting it as one discounts the piece a weapon's %ATT lines are worth
// the most on.
//
// Meso spent cubing one of these still buys the climb toward its replacement,
// so the gain is discounted rather than refused.
bool Replaceable(const GameState& state, EquipSlot slot);

}  // namespace ms

#endif  // MS_ANALYSIS_CUBE_PLAN_H_
