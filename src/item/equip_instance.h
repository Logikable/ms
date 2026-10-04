/* EquipInstance wraps one piece of equipment. It pairs an EquipPrototype (the
 * static item definition from data/) with an Equip proto (per-item state:
 * remaining upgrade slots, scroll stats and star force level). It adds the
 * mutating methods (Scroll, StarForce, Cube) to the read-only base class
 * EquipTabItem (see item.h). EquipTrace, also in item.h, is the type for
 * destroyed items.
 */
#ifndef MS_SRC_ITEM_EQUIP_INSTANCE_H_
#define MS_SRC_ITEM_EQUIP_INSTANCE_H_

#include <cstdint>
#include <memory>
#include <random>

#include "src/item/flame.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/item/soul.h"
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
  // omitted (fresh drop), it starts as FreshEquip(prototype).
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

  // Rerolls the potential the cube's track names without charging; the caller
  // takes the meso, as with star force. The first cube on an item always gives
  // a Rare potential; each later one rolls for a rank-up first. Returns false
  // and changes nothing if the item can't have potential; see CanCube.
  bool Cube(CubeType cube, std::mt19937& rng);

  // Sets `potential` on this item, when a player accepts an offered roll; see
  // CharacterInstance::BuyCube. Nothing is checked, since the roll came from
  // this item's own group.
  void SetPotential(PotentialTrack track, const Potential& potential) {
    *MutablePotentialOf(state_, track) = potential;
  }

  // Whether a cube can be used on this item: it's worn in a slot potential
  // applies to and doesn't refuse cubes. A trace never qualifies, since it
  // isn't an EquipInstance.
  bool CanCube() const {
    return SlotTakesPotential(prototype_.equip_slot()) &&
           Supports(prototype_, UPGRADE_CUBE);
  }

  // Whether a flame can be used on this item: it's worn in a slot flames apply
  // to and doesn't refuse them.
  bool CanFlame() const {
    return SlotTakesFlame(prototype_.equip_slot()) &&
           Supports(prototype_, UPGRADE_FLAME);
  }

  // Replaces the item's flame. Nothing is checked, since the lines came from
  // this item's own pool; see CharacterInstance::BuyFlame.
  void SetFlame(const FlameLines& lines) {
    *state_.mutable_flame() = lines;
  }

  // Whether a soul can be applied: a primary weapon that doesn't refuse it.
  bool CanTakeSoul() const {
    return TakesSoul(prototype_);
  }

  // Replaces the item's soul, as applying another one does in GMS.
  void SetSoul(const Soul& soul) {
    *state_.mutable_soul() = soul;
  }

  // Returns the star force attempt rates for the given star level.
  // Returns {0, 0} for out-of-range values.
  static StarForceRate RateAt(int stars);

  // The number of stars a trace recovery gives, based on the item's stars when
  // it was destroyed: 15–19→12, 20→15, 21–22→17, 23–25→19, 26–30→20. Returns 0
  // below 15 stars (not destroyable).
  static int RecoveryStars(int original_stars);

  // False for an item that takes no star force, one with upgrade slots left
  // (scrolling comes first), or one already at max stars. The first case is why
  // this isn't just a slot count: an item with no slots has nothing to scroll
  // and would look ready.
  bool CanStarForce() const {
    return Supports(prototype_, UPGRADE_STAR_FORCE) &&
           state_.remaining_upgrade_slots() == 0 &&
           state_.stars() < max_stars();
  }
};

}  // namespace ms

#endif  // MS_SRC_ITEM_EQUIP_INSTANCE_H_
