#ifndef MS_SRC_EQUIP_INSTANCE_H_
#define MS_SRC_EQUIP_INSTANCE_H_

#include <random>

#include "src/equip.pb.h"
#include "src/equip_stats.h"
#include "src/scroll.pb.h"

namespace ms {

class EquipInstance {
 public:
  explicit EquipInstance(const EquipPrototype& prototype);

  // Consumes one upgrade slot and rolls against the scroll's success_rate.
  // Adds the scroll's stats on success. Returns true on success, false on
  // failure or if no slots remain.
  bool Scroll(const ms::Scroll& scroll, std::mt19937& rng);

  const EquipPrototype& prototype() const { return prototype_; }
  const Equip& proto() const { return state_; }
  // Returns prototype base stats plus all scroll stats accumulated so far.
  EquipStats stats() const;

 private:
  const EquipPrototype& prototype_;
  Equip state_;
};

}  // namespace ms

#endif  // MS_SRC_EQUIP_INSTANCE_H_
