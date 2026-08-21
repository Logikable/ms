#include "src/item/equip_instance.h"

#include <random>

#include "src/item/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// Success and destruction rates in hundredths of a percent (10000 = 100%).
// Index i = attempt from i★ to (i+1)★. Failure = 10000 - success - destroy.
constexpr StarForceRate kRates[kMaxStarForce] = {
    {9500, 0},     // 0★
    {9000, 0},     // 1★
    {8500, 0},     // 2★
    {8500, 0},     // 3★
    {8000, 0},     // 4★
    {7500, 0},     // 5★
    {7000, 0},     // 6★
    {6500, 0},     // 7★
    {6000, 0},     // 8★
    {5500, 0},     // 9★
    {5000, 0},     // 10★
    {4500, 0},     // 11★
    {4000, 0},     // 12★
    {3500, 0},     // 13★
    {3000, 0},     // 14★
    {3000, 210},   // 15★
    {3000, 210},   // 16★
    {1500, 680},   // 17★
    {1500, 680},   // 18★
    {1500, 850},   // 19★
    {3000, 1050},  // 20★
    {1500, 1275},  // 21★
    {1500, 1700},  // 22★
    {1000, 1800},  // 23★
    {1000, 1800},  // 24★
    {1000, 1800},  // 25★
    {700, 1860},   // 26★
    {500, 1900},   // 27★
    {300, 1940},   // 28★
    {100, 1980},   // 29★
};

}  // namespace

ScrollTier TierForLevel(int required_level) {
  if (required_level >= 115) {
    return SCROLL_TIER_3;
  }
  if (required_level >= 75) {
    return SCROLL_TIER_2;
  }
  return SCROLL_TIER_1;
}

ScrollTarget TargetForSlot(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return SCROLL_TARGET_WEAPON;
    case EQUIP_SLOT_HAT:
    case EQUIP_SLOT_TOP:
    case EQUIP_SLOT_BOTTOM:
    case EQUIP_SLOT_CAPE:
      return SCROLL_TARGET_ARMOUR;
    case EQUIP_SLOT_FACE_ACCESSORY:
    case EQUIP_SLOT_EYE_ACCESSORY:
      return SCROLL_TARGET_ACCESSORY;
    case EQUIP_SLOT_UNSPECIFIED:
    case EQUIP_SLOT_PROJECTILE:
    case EQUIP_SLOT_SECONDARY:
      return SCROLL_TARGET_UNSPECIFIED;
  }
  return SCROLL_TARGET_UNSPECIFIED;
}

EquipInstance::EquipInstance(const EquipPrototype& prototype,
                             const Equip& state)
    : EquipTabItem(prototype, state) {
  // Default Equip (fresh drop) has empty equip_name; initialize from prototype.
  if (state_.equip_name().empty()) {
    state_.set_equip_name(prototype_.name());
    state_.set_remaining_upgrade_slots(prototype_.upgrade_slots());
  }
}

ScrollOutcome EquipInstance::Scroll(const ms::Scroll& scroll,
                                    std::mt19937& rng) {
  // An item with no slots already refuses every scroll below, but saying it
  // once up here covers the item that declares itself unscrollable while still
  // carrying slots.
  if (!Supports(prototype_, UPGRADE_SCROLL)) {
    return kScrollNoSlots;
  }
  if (scroll.scroll_category() == SCROLL_CATEGORY_CLEAN_SLATE) {
    // TODO: golden hammers will not work against this. The cap counts the
    // prototype's slots, so a hammer slot added to the instance cannot be
    // bought back once it fails -- the cap has to come from the instance's own
    // total once hammers exist. //analysis:scroll_cost_sim prices the job
    // assuming they do.
    int cap = prototype_.upgrade_slots() - state_.scroll_successes();
    if (state_.remaining_upgrade_slots() >= cap) {
      return kScrollNoSlots;
    }
    state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() + 1);
    return kScrollSuccess;
  }
  if (state_.remaining_upgrade_slots() == 0) {
    return kScrollNoSlots;
  }
  state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() - 1);
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return kScrollFail;
  }
  state_.set_scroll_successes(state_.scroll_successes() + 1);
  const EquipStats scroll_sources[] = {state_.scroll_stats(), scroll.stats()};
  *state_.mutable_scroll_stats() = SumEquipStats(scroll_sources);
  return kScrollSuccess;
}

StarForceOutcome EquipInstance::StarForce(std::mt19937& rng) {
  // Checked here as well as in CanStarForce so an item that refuses stars
  // refuses them whatever the caller believed.
  if (!Supports(prototype_, UPGRADE_STAR_FORCE)) {
    return kStarForceFail;
  }
  if (state_.remaining_upgrade_slots() > 0) {
    return kStarForceFail;
  }
  int s = state_.stars();
  if (s >= max_stars()) {
    return kStarForceFail;
  }
  const StarForceRate& rate = kRates[s];
  std::uniform_int_distribution<int> dist(1, 10000);
  int roll = dist(rng);
  if (roll <= rate.success) {
    state_.set_stars(s + 1);
    return kStarForceSuccess;
  }
  if (rate.destroy > 0 && roll > 10000 - rate.destroy) {
    return kStarForceDestroy;
  }
  return kStarForceFail;
}

StarForceRate EquipInstance::RateAt(int stars) {
  if (stars < 0 || stars >= kMaxStarForce) {
    return {0, 0};
  }
  return kRates[stars];
}

int EquipInstance::RecoveryStars(int original_stars) {
  if (original_stars >= 26) {
    return 20;
  }
  if (original_stars >= 23) {
    return 19;
  }
  if (original_stars >= 21) {
    return 17;
  }
  if (original_stars == 20) {
    return 15;
  }
  if (original_stars >= 15) {
    return 12;
  }
  return 0;
}

}  // namespace ms
