/* Potential: three rolled lines an item carries, and the cube that rerolls
 * them.
 *
 * Every line has a rank, and the rank with the line's type and the item's
 * level decides what it is worth -- GMS states most lines as a value per
 * equipment level band, so the value is a function of the three and is never
 * stored.
 *
 * The potential as a whole has a rank too. Its first line always carries that
 * rank, the other two are prime at long odds and a rung below otherwise, and a
 * cube can carry the whole potential up a rank but never down.
 *
 * Which lines an item can roll is decided by where it is worn: only a hat
 * gives cooldown, only gloves give critical damage, only a weapon, secondary
 * or emblem gives %ATT, and only an accessory gives %meso or %drop. See
 * PotentialGroupOf.
 *
 * Pure math over the protos, like inner_ability.h. Charging for the cube is
 * the caller's business, as it is for star force.
 */
#ifndef MS_SRC_ITEM_POTENTIAL_H_
#define MS_SRC_ITEM_POTENTIAL_H_

#include <cstdint>
#include <random>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {

// The level cubing opens at. Account-wide, like every other upgrade: a player
// who has taken one character to it can cube on all of them.
inline constexpr int kPotentialUnlockLevel = 180;

// What one cube takes, whatever it rolls. Flat, as GMS prices it: a cube on a
// level 200 weapon costs what a cube on a level 100 ring costs, so the piece
// worth cubing is the piece whose lines are worth the most.
inline constexpr int64_t kCubeCost = 12'000'000;

// Lines every potential has. GMS reveals them one at a time and sells a stamp
// for the third; here an item always has all three.
inline constexpr int kPotentialLines = 3;

// The pool an item draws from, which is what the slot it is worn in comes to.
// Hat and gloves are apart from the rest of the armour only because each has
// one line of its own; everything they share is written against all four of
// the non-weapon groups.
enum class PotentialGroup {
  kNone,
  kWeaponry,
  kHat,
  kGloves,
  kArmor,
  kAccessory,
};

// Where an item worn in `slot` draws its lines from. kNone for the slots that
// take no potential at all: the projectile, the six symbols, the badge, the
// medal and the pocket.
PotentialGroup PotentialGroupOf(EquipSlot slot);

// Whether an item worn in `slot` can be cubed at all.
bool SlotTakesPotential(EquipSlot slot);

// A cube, which is the odds and the price rather than an item: nothing is
// carried in the bag, and the name is what the player is buying.
enum class CubeType {
  kRed,
};

// Which of an item's two potentials a cube rerolls. Bonus potential is not
// built -- see the note on Equip.main_potential -- so no cube names it yet.
enum class PotentialTrack {
  kMain,
  kBonus,
};

// One cube on the shelf: what it rerolls and what it takes. The list the
// cubing screen offers, in the order it offers them.
struct Cube {
  CubeType type;
  PotentialTrack track;
  int64_t cost;
};

inline constexpr Cube kCubes[] = {
    {CubeType::kRed, PotentialTrack::kMain, kCubeCost},
};

// The shelf entry for `type`.
const Cube& CubeOf(CubeType type);

// The rank above `rank`, and the rank below. Both stop at the end they run
// into: nothing climbs past Legendary, and a non-prime line on a Rare
// potential is Rare, there being nothing under it.
PotentialRank NextPotentialRank(PotentialRank rank);
PotentialRank PreviousPotentialRank(PotentialRank rank);

// Chance one use of `cube` carries a potential at `rank` to the rank above.
// Zero at Legendary, which is the top.
double PotentialRankUpChance(CubeType cube, PotentialRank rank);

// Chance the line at `index` comes out prime -- carrying the potential's own
// rank rather than the one below. Always 1 for the first line, which is what
// makes the rank of a potential visible at all.
double PotentialPrimeChance(CubeType cube, int index);

// What `type` at `rank` is worth on an item of `item_level`: flat for the
// stats and Max HP, whole percents for the rest, and seconds for the two
// cooldown lines. Zero for a pairing that does not roll.
int PotentialLineValue(PotentialLineType type, PotentialRank rank,
                       int item_level);

// The lines an item of `group` can roll at `rank`, in catalog order. Every
// one of them is equally likely; GMS's own weights are dropped along with the
// junk lines they mostly sat on, and so are its per-line equipment level
// requirements -- nothing is carrying a Legendary potential on a level 30
// item.
std::vector<PotentialLineType> PotentialPool(PotentialGroup group,
                                             PotentialRank rank);

// Rolls a whole potential at `rank`. The first line carries the rank, the
// other two are prime at the cube's odds and a rung below otherwise.
// Duplicate lines are allowed: three lots of %ATT on one weapon is the point.
Potential RollPotential(CubeType cube, PotentialGroup group, PotentialRank rank,
                        std::mt19937& rng);

// What the potentials on everything worn come to. The flat lines arrive in
// the shape of a worn item, since that is how they behave; every percentage
// is a fraction, as DerivedStats states them, so the fold that reads this has
// nothing to convert.
struct PotentialTotals {
  EquipStats flat;
  // The stat shares. What they multiply is not settled here -- see
  // AddPotentials in character_stats.cc, which is the one place that knows
  // which stats a potential is allowed to scale.
  double str_pct = 0.0;
  double dex_pct = 0.0;
  double int_pct = 0.0;
  double luk_pct = 0.0;
  double max_hp_pct = 0.0;
  // Attack and magic attack are apart, as GMS has them: a weapon's %ATT line
  // is worth nothing to a magician.
  double attack_pct = 0.0;
  double magic_attack_pct = 0.0;
  double damage_pct = 0.0;
  double boss_pct = 0.0;
  // Combined in reverse across the lines granting it, the way two sources of
  // ignored defence always meet.
  double ied = 0.0;
  double crit_dmg = 0.0;
  double meso_pct = 0.0;
  double item_drop_pct = 0.0;
  // Seconds off every skill's wait. Summed: a hat can carry both lines.
  double cooldown_seconds = 0.0;
};

// Adds what `potential` grants on an item of `item_level` to `totals`.
void AddPotential(const Potential& potential, int item_level,
                  PotentialTotals& totals);

// One use of `cube` on an item holding `current`. An item with no potential
// is handed a Rare one, whatever the cube -- there is no rank roll on the
// first use. Otherwise the potential rolls for its rank up and every line is
// thrown away and rolled again.
Potential CubePotential(const Potential& current, CubeType cube,
                        PotentialGroup group, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_ITEM_POTENTIAL_H_
