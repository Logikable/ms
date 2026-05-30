#include "src/equip_instance.h"

#include <random>

namespace ms {

EquipInstance::EquipInstance(const EquipPrototype& prototype)
    : prototype_(prototype) {
  state_.set_equip_name(prototype.name());
  state_.set_remaining_upgrade_slots(prototype.upgrade_slots());
}

bool EquipInstance::Scroll(const ms::Scroll& scroll, std::mt19937& rng) {
  if (state_.remaining_upgrade_slots() == 0) {
    return false;
  }
  state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() - 1);
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return false;
  }
  *state_.mutable_scroll_stats() =
      SumEquipStats({state_.scroll_stats(), scroll.stats()});
  return true;
}

EquipStats EquipInstance::stats() const {
  return SumEquipStats({prototype_.base_stats(), state_.scroll_stats()});
}

}  // namespace ms
