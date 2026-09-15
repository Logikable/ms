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

#include <cstdint>
#include <functional>
#include <random>

#include "analysis/yardstick.h"
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
  // The defence of the fight the character is aimed at, as a fraction --
  // The fight the lines are judged against. What an ignored-defence line is
  // worth is a fact about that fight and not about the character, and it moves
  // by a factor of three between Cygnus and Lotus -- so the monster itself is
  // carried rather than a number standing for it. See //analysis:yardstick,
  // which replaced a hand-folded defence factor here.
  Yardstick yard;
};

CubeBasis CubeBasisFor(const GameState& state);

// A run of cubes into one slot, and what the run is expected to leave behind.
//
// A PROGRAM rather than a single cube, because what one cube is worth is not
// what cubing a slot is worth. A character short of a boss's defence wall
// gains exactly nothing from any one roll -- both sides of it are on the
// 1-damage floor -- while sixty rolls have a real chance at the line that
// clears the wall. Priced one at a time, the slot that most needs cubing is
// the one that never gets a cube.
struct CubeProgram {
  int cubes = 0;      // how many the run buys
  double gain = 0.0;  // what the best roll of the run is expected to add
  int64_t cost = 0;   // what the run costs altogether

  bool worth() const {
    return cubes > 0 && gain > 0.0 && cost > 0;
  }
};

// The run into `slot` that pays best per meso, out of a ladder of lengths.
// Empty where the slot takes no potential or holds nothing a cube improves.
//
// The whole ladder comes off ONE sample of rolls: what a run of N leaves is
// the best of N draws, and the chance that the best of N is the i-th of a
// sorted sample is (i/m)^N - ((i-1)/m)^N. So a sixty-cube program costs no
// more to price than a one-cube one.
CubeProgram BestCubeProgram(const GameState& state, const CubeBasis& basis,
                            EquipSlot slot, const CubeIncome& income,
                            std::mt19937& rng);

// Whether `rolled` beats what `slot` already holds -- the same comparison
// BestCubeProgram averages over, asked once of a roll in hand. A cube is paid
// either way, so this decides only what the item ends up wearing.
bool WorthTaking(const GameState& state, const CubeBasis& basis, EquipSlot slot,
                 const Potential& rolled, const CubeIncome& income);

// Whether the shopper is likely to replace what `slot` holds: the catalog
// offers a piece for that slot at a higher level which the character can
// already wear AND could pay for. A weapon has to match the type in hand as
// well -- a Lv140 sword is not a replacement for a Lv120 axe a Hero measured
// their way into, and counting it as one discounts the piece a weapon's %ATT
// lines are worth the most on.
//
// Listed is not the same as reachable. A tier priced in a token counts only
// once one of that token is in the bag, so a character locked out of the fight
// that drops it cubes what they are holding rather than saving for scenery.
//
// Meso spent cubing one of these still buys the climb toward its replacement,
// so the gain is discounted rather than refused.
bool Replaceable(const GameState& state, EquipSlot slot);

}  // namespace ms

#endif  // MS_ANALYSIS_CUBE_PLAN_H_
