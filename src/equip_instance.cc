#include "src/equip_instance.h"

#include <random>

namespace ms {

EquipInstance::EquipInstance(EquipPrototype prototype)
    : prototype_(std::move(prototype)) {
  state_.set_equip_name(prototype_.name());
  state_.set_remaining_upgrade_slots(prototype_.upgrade_slots());
}

EquipInstance::EquipInstance(EquipPrototype prototype, Equip state)
    : prototype_(std::move(prototype)), state_(std::move(state)) {}

bool EquipInstance::Scroll(const ms::Scroll& scroll, std::mt19937& rng) {
  if (state_.remaining_upgrade_slots() == 0) {
    return false;
  }
  state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() - 1);
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return false;
  }
  const EquipStats scroll_sources[] = {state_.scroll_stats(), scroll.stats()};
  *state_.mutable_scroll_stats() = SumEquipStats(scroll_sources);
  return true;
}

EquipStats EquipInstance::stats() const {
  const EquipStats stat_sources[] = {prototype_.base_stats(), state_.scroll_stats()};
  return SumEquipStats(stat_sources);
}

}  // namespace ms
