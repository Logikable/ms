/* Potential: three rolled lines an item has, and the cube that rerolls them.
 *
 * Every line has a rank, and the rank, line type and item level together decide
 * its value. GMS states most lines as a value per equipment level band, so the
 * value is computed from those three and never stored.
 *
 * The potential as a whole has a rank too. Its first line always has that rank;
 * the other two are prime at low odds and one rank lower otherwise. A cube can
 * raise the whole potential's rank but never lower it.
 *
 * The slot an item is worn in decides which lines it can roll: only hats give
 * cooldown, only gloves give critical damage, only weapons, secondaries and
 * emblems give %ATT, and only accessories give %meso or %drop. See
 * PotentialGroupOf.
 *
 * Pure math over the protos, like inner_ability.h. The caller charges for the
 * cube, as with star force.
 */
#ifndef MS_SRC_ITEM_POTENTIAL_H_
#define MS_SRC_ITEM_POTENTIAL_H_

#include <cstdint>
#include <random>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {

// The level cubing unlocks at. Account-wide, like every other upgrade: a player
// who has taken one character there can cube on all of them.
inline constexpr int kPotentialUnlockLevel = 180;

// The cost of one cube, whatever it rolls. Flat, as GMS prices it: a cube on a
// level 200 weapon costs the same as on a level 100 ring, so the item worth
// cubing is the one whose lines are worth the most.
inline constexpr int64_t kCubeCost = 12'000'000;

// Lines every potential has. GMS reveals them one at a time and sells an item
// for the third; here an item always has all three.
inline constexpr int kPotentialLines = 3;

// The pool an item rolls from, determined by its slot. Hats and gloves are
// separate from other armour only because each has one unique line; everything
// they share is listed against all four non-weapon groups.
enum class PotentialGroup {
  kNone,
  kWeaponry,
  kHat,
  kGloves,
  kArmor,
  kAccessory,
};

// The pool an item worn in `slot` rolls from. kNone for slots that take no
// potential: the projectile, the six symbols, the badge, the medal and the
// pocket.
PotentialGroup PotentialGroupOf(EquipSlot slot);

// Whether an item worn in `slot` can be cubed at all.
bool SlotTakesPotential(EquipSlot slot);

// A cube type: odds and a price, not an item. Nothing goes in the bag, and the
// name is what the player is buying.
enum class CubeType {
  kRed,
};

// Which of an item's two potentials a cube rerolls. Bonus potential isn't built
// (see the note on Equip.main_potential), so no cube uses it yet.
enum class PotentialTrack {
  kMain,
  kBonus,
};

// One cube in the shop: what it rerolls and what it costs. The list the cubing
// screen offers, in its order.
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

// The rank above `rank`, and the rank below. Both stop at the ends: nothing
// goes past Legendary, and a non-prime line on a Rare potential stays Rare,
// since there's nothing below it.
PotentialRank NextPotentialRank(PotentialRank rank);
PotentialRank PreviousPotentialRank(PotentialRank rank);

// Chance one use of `cube` raises a potential at `rank` to the next rank. Zero
// at Legendary, the top.
double PotentialRankUpChance(CubeType cube, PotentialRank rank);

// Chance the line at `index` is prime, meaning it has the potential's own rank
// instead of the one below. Always 1 for the first line, which is what makes
// the potential's rank visible.
double PotentialPrimeChance(CubeType cube, int index);

// The value of `type` at `rank` on an item of `item_level`: flat for stats and
// Max HP, whole percents for the rest, and seconds for the two cooldown lines.
// Zero for a combination that doesn't roll.
int PotentialLineValue(PotentialLineType type, PotentialRank rank,
                       int item_level);

// The lines an item of `group` can roll at `rank`, in catalog order. All are
// equally likely: GMS's weights are dropped along with the junk lines they
// mostly applied to, and so are its per-line equipment level requirements,
// since nothing has a Legendary potential on a level 30 item.
std::vector<PotentialLineType> PotentialPool(PotentialGroup group,
                                             PotentialRank rank);

// Rolls a whole potential at `rank`. The first line has the rank; the other two
// are prime at the cube's odds and one rank lower otherwise. Duplicate lines
// are allowed: three %ATT lines on one weapon is the goal.
Potential RollPotential(CubeType cube, PotentialGroup group, PotentialRank rank,
                        std::mt19937& rng);

// The combined potentials on everything worn. Flat lines are shaped like a worn
// item's stats, since they behave that way; every percentage is a fraction, as
// DerivedStats uses them, so the code reading this needs no conversion.
struct PotentialTotals {
  EquipStats flat;
  // Stat percentages. What they multiply isn't decided here; see AddPotentials
  // in character_stats.cc, the one place that knows which stats potential can
  // scale.
  double str_pct = 0.0;
  double dex_pct = 0.0;
  double int_pct = 0.0;
  double luk_pct = 0.0;
  double max_hp_pct = 0.0;
  // Attack and magic attack are separate, as in GMS: a weapon's %ATT line is
  // useless to a magician.
  double attack_pct = 0.0;
  double magic_attack_pct = 0.0;
  double damage_pct = 0.0;
  double boss_pct = 0.0;
  // Combined multiplicatively across the lines granting it, the way ignored
  // defence always combines.
  double ied = 0.0;
  double crit_dmg = 0.0;
  double meso_pct = 0.0;
  double item_drop_pct = 0.0;
  // Seconds off every skill's cooldown. Summed, since a hat can have both
  // lines.
  double cooldown_seconds = 0.0;
};

// Adds what `potential` grants on an item of `item_level` to `totals`.
void AddPotential(const Potential& potential, int item_level,
                  PotentialTotals& totals);

// One use of `cube` on an item with `current`. An item with no potential gets a
// Rare one, whatever the cube, with no rank roll on the first use. Otherwise
// the potential rolls for a rank-up and every line is rerolled.
Potential CubePotential(const Potential& current, CubeType cube,
                        PotentialGroup group, std::mt19937& rng);

}  // namespace ms

#endif  // MS_SRC_ITEM_POTENTIAL_H_
