#include "src/equip_instance.h"

#include <random>

namespace ms {

EquipInstance::EquipInstance(const Equip& prototype)
    : prototype_(prototype),
      remaining_upgrade_slots_(prototype.upgrade_slots()) {}

bool EquipInstance::Scroll(const ms::Scroll& scroll, std::mt19937& rng) {
  if (remaining_upgrade_slots_ == 0) {
    return false;
  }
  --remaining_upgrade_slots_;
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return false;
  }
  scroll_stats_ = SumEquipStats({scroll_stats_, scroll.stats()});
  return true;
}

EquipStats EquipInstance::stats() const {
  return SumEquipStats({prototype_.base_stats(), scroll_stats_});
}

}  // namespace ms
