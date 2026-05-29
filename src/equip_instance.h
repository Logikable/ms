#ifndef MS_SRC_EQUIP_INSTANCE_H_
#define MS_SRC_EQUIP_INSTANCE_H_

#include <random>

#include "src/equip.pb.h"
#include "src/equip_stats.h"
#include "src/scroll.pb.h"

namespace ms {

class EquipInstance {
 public:
  explicit EquipInstance(const Equip& prototype);

  // Consumes one upgrade slot and rolls against the scroll's success_rate.
  // Adds the scroll's stats on success. Returns true on success, false on
  // failure or if no slots remain.
  bool Scroll(const ms::Scroll& scroll, std::mt19937& rng);

  const Equip& prototype() const { return prototype_; }
  int remaining_upgrade_slots() const { return remaining_upgrade_slots_; }
  EquipStats stats() const;

 private:
  const Equip& prototype_;
  int remaining_upgrade_slots_;
  EquipStats scroll_stats_;
};

}  // namespace ms

#endif  // MS_SRC_EQUIP_INSTANCE_H_
