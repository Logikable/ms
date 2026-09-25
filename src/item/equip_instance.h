/* EquipInstance wraps one piece of equipment. It pairs an EquipPrototype (the
 * static item definition from data/) with an Equip proto (per-item state:
 * remaining upgrade slots, scroll stats and star force level). It adds the
 * mutating methods (Scroll, StarForce, Hammer) to the read-only base class
 * EquipTabItem (see item.h). EquipTrace, also in item.h, is the type for
 * destroyed items.
 */
#ifndef MS_SRC_ITEM_EQUIP_INSTANCE_H_
#define MS_SRC_ITEM_EQUIP_INSTANCE_H_

#include <cstdint>
#include <memory>
#include <random>

#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

enum ScrollOutcome : int { kScrollSuccess, kScrollFail, kScrollNoSlots };

// Which tier of scroll an item of this required level takes. GMS's cutoffs:
// T1 below 75, T2 75-114, T3 115 and up.
ScrollTier TierForLevel(int required_level);

// Which scrolls an item worn in this slot takes. Unspecified for slots whose
// items refuse scrolls anyway (stars and the secondary), so a scroll for
// neither weapon nor armour applies to nothing.
ScrollTarget TargetForSlot(EquipSlot slot);
// kStarForceNoMeso is the only outcome that isn't a roll: the attempt didn't
// happen, so the item is untouched and nothing was spent.
enum StarForceOutcome {
  kStarForceSuccess,
  kStarForceFail,
  kStarForceDestroy,
  kStarForceNoMeso
};

// Absolute maximum star force level (for level 138+ equipment).
constexpr int kMaxStarForce = 30;

// Golden hammers one piece of equipment can take, and their cost. The price is
// flat: a hammer adds the same slot to any item, so nothing about the item
// affects it.
constexpr int kMaxHammers = 2;
constexpr int64_t kGoldenHammerCost = 10000000;

// Success and destruction rates for a single star force attempt, in hundredths
// of a percent (10000 = 100%). Failure = 10000 - success - destroy.
struct StarForceRate {
  int success;
  int destroy;
};

// The equip-tab item a saved state describes. A trace and a live item have the
// same fields except a flag, which decides the type, so this is the one place
// that reads it, whether the state came from a save file or a trade.
std::unique_ptr<EquipTabItem> EquipItemFromState(const EquipPrototype& proto,
                                                 const Equip& state);

class EquipInstance : public EquipTabItem {
 public:
  // Constructs from a prototype and optional existing state. When state is
  // omitted (fresh drop), equip_name and remaining_upgrade_slots are
  // initialized from the prototype.
  explicit EquipInstance(const EquipPrototype& prototype,
                         const Equip& state = {});
  std::unique_ptr<EquipTabItem> Clone() const override;

  // Consumes one upgrade slot and rolls against the scroll's success_rate.
  // Adds the scroll's stats on success. Returns kScrollNoSlots if no slots
  // remain, kScrollSuccess on success, or kScrollFail on failure.
  ScrollOutcome Scroll(const ms::Scroll& scroll, std::mt19937& rng);

  // Attempts a star force upgrade. Increments stars on success; does not
  // modify state on fail or destroy (caller removes the item on destroy).
  // Returns kStarForceFail if already at max_stars().
  StarForceOutcome StarForce(std::mt19937& rng);

  // Rerolls this item's main potential without charging; the caller takes the
  // meso, as with star force. The first cube on an item always gives a Rare
  // potential; each later one rolls for a rank-up first. Returns false and
  // changes nothing if the item can't have potential; see CanCube.
  bool Cube(CubeType cube, std::mt19937& rng);

  // Sets `potential` on this item, when a player accepts an offered roll; see
  // CharacterInstance::BuyCube. Nothing is checked, since the roll came from
  // this item's own group.
  void SetPotential(const Potential& potential) {
    *state_.mutable_main_potential() = potential;
  }

  // Whether a cube can be used on this item, meaning it's worn in a slot
  // potential applies to. A trace never qualifies, since it isn't an
  // EquipInstance.
  bool CanCube() const {
    return SlotTakesPotential(prototype_.equip_slot());
  }

  // Uses a golden hammer: one more upgrade slot, open and unspent. Returns
  // false and changes nothing if the item can't take another; see CanHammer.
  // The caller charges for it, as with star force.
  bool Hammer();

  // Returns the star force attempt rates for the given star level.
  // Returns {0, 0} for out-of-range values.
  static StarForceRate RateAt(int stars);

  // The number of stars a trace recovery gives, based on the item's stars when
  // it was destroyed: 15–19→12, 20→15, 21–22→17, 23–25→19, 26–30→20. Returns 0
  // below 15 stars (not destroyable).
  static int RecoveryStars(int original_stars);

  // Whether another hammer can be used: the item has upgrade slots to widen and
  // isn't at kMaxHammers. The item's progress doesn't matter; a hammer can be
  // used at any point, even with stars.
  bool CanHammer() const {
    return TakesUpgradeSlots(prototype_) && state_.hammers() < kMaxHammers;
  }

  // False for an item that takes no star force, one with upgrade slots left
  // (scrolling comes first), or one already at max stars. The first case is why
  // this isn't just a slot count: an item with no slots has nothing to scroll
  // and would look ready. A hammer adds a slot, so hammering a starred item
  // blocks its stars: the same rule, not an exception.
  bool CanStarForce() const {
    return Supports(prototype_, UPGRADE_STAR_FORCE) &&
           state_.remaining_upgrade_slots() == 0 &&
           state_.stars() < max_stars();
  }
};

}  // namespace ms

#endif  // MS_SRC_ITEM_EQUIP_INSTANCE_H_
