/* EquipInstance wraps a single in-game drop of equipment. It pairs an
 * EquipPrototype (static item definition loaded from data/) with an Equip proto
 * (per-instance mutable state: remaining upgrade slots, accumulated scroll
 * stats, and star force level). equip_instance.cc implements scrolling,
 * star forcing, and stat aggregation.
 */
#ifndef MS_SRC_EQUIP_INSTANCE_H_
#define MS_SRC_EQUIP_INSTANCE_H_

#include <random>

#include "src/equip_stats.h"
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

class EquipInstance {
 public:
  explicit EquipInstance(EquipPrototype prototype);

  // Constructs from an existing saved state rather than a fresh drop.
  EquipInstance(EquipPrototype prototype, Equip state);

  // Consumes one upgrade slot and rolls against the scroll's success_rate.
  // Adds the scroll's stats on success. Returns kScrollNoSlots if no slots
  // remain, kScrollSuccess on success, or kScrollFail on failure.
  ScrollOutcome Scroll(const ms::Scroll& scroll, std::mt19937& rng);

  // Attempts a star force upgrade. Increments stars on success; does not
  // modify state on fail or destroy (caller removes the item on destroy).
  // Returns kStarForceFail if already at max_stars().
  StarForceOutcome StarForce(std::mt19937& rng);

  // Returns the star force attempt rates for the given star level.
  static StarForceRate RateAt(int stars);
  // Returns the maximum star force level for the given required_level, per the
  // GMS equipment-level scaling table.
  static int MaxStarsForLevel(int required_level);

  bool CanStarForce() const {
    return state_.stars() < max_stars();
  }
  int max_stars() const {
    return MaxStarsForLevel(prototype_.required_level());
  }
  const EquipPrototype& prototype() const {
    return prototype_;
  }
  const Equip& proto() const {
    return state_;
  }
  int stars() const {
    return state_.stars();
  }

  // Returns prototype base stats plus all scroll stats accumulated so far.
  EquipStats stats() const;

 private:
  EquipPrototype prototype_;
  Equip state_;
};

}  // namespace ms

#endif  // MS_SRC_EQUIP_INSTANCE_H_
