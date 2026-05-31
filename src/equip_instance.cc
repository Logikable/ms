#include "src/equip_instance.h"

namespace ms {

EquipInstance::EquipInstance(const Equip& prototype)
    : prototype_(prototype) {}

bool EquipInstance::Scroll() {
  return false;
}

EquipStats EquipInstance::stats() const {
  return SumEquipStats({prototype_.base_stats(), scroll_stats_});
}

}  // namespace ms
