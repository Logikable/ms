/* EquipInstance wraps a single in-game drop of equipment. It pairs an
 * EquipPrototype (static item definition loaded from data/) with an Equip proto
 * (per-instance mutable state: remaining upgrade slots, accumulated scroll
 * stats, and star force level). It adds mutation methods (Scroll, StarForce)
 * on top of the read-only base class EquipTabItem (see item.h).
 * EquipTrace (also in item.h) is the companion type for destroyed items.
 */
#ifndef MS_SRC_EQUIP_INSTANCE_H_
#define MS_SRC_EQUIP_INSTANCE_H_

#include <random>

#include "src/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

enum ScrollOutcome : int { kScrollSuccess, kScrollFail, kScrollNoSlots };
enum StarForceOutcome { kStarForceSuccess, kStarForceFail, kStarForceDestroy };

// Absolute maximum star force level (for level 138+ equipment).
constexpr int kMaxStarForce = 30;

// Success and destruction rates for a single star force attempt, in hundredths
// of a percent (10000 = 100%). Failure = 10000 - success - destroy.
struct StarForceRate {
  int success;
  int destroy;
};

class EquipInstance : public EquipTabItem {
 public:
  // Constructs from a prototype and optional existing state. When state is
  // omitted (fresh drop), equip_name and remaining_upgrade_slots are
  // initialized from the prototype.
  explicit EquipInstance(const EquipPrototype& prototype,
                         const Equip& state = {});

  // Consumes one upgrade slot and rolls against the scroll's success_rate.
  // Adds the scroll's stats on success. Returns kScrollNoSlots if no slots
  // remain, kScrollSuccess on success, or kScrollFail on failure.
  ScrollOutcome Scroll(const ms::Scroll& scroll, std::mt19937& rng);

  // Attempts a star force upgrade. Increments stars on success; does not
  // modify state on fail or destroy (caller removes the item on destroy).
  // Returns kStarForceFail if already at max_stars().
  StarForceOutcome StarForce(std::mt19937& rng);

  // Returns the star force attempt rates for the given star level.
  // Returns {0, 0} for out-of-range values.
  static StarForceRate RateAt(int stars);

  // Returns false if upgrade slots remain (scrolling must be completed first)
  // or if already at max stars.
  bool CanStarForce() const {
    return state_.remaining_upgrade_slots() == 0 &&
           state_.stars() < max_stars();
  }
};

}  // namespace ms

#endif  // MS_SRC_EQUIP_INSTANCE_H_
