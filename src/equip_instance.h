#ifndef MS_SRC_EQUIP_INSTANCE_H_
#define MS_SRC_EQUIP_INSTANCE_H_

#include "src/equip.pb.h"
#include "src/equip_stats.h"

namespace ms {

class EquipInstance {
 public:
  explicit EquipInstance(const Equip& prototype);

  // Applies a scroll, consuming one upgrade slot. Returns false if no
  // slots remain. Signature will expand once Scroll is modeled.
  bool Scroll();

  const Equip& prototype() const { return prototype_; }
  EquipStats stats() const;

 private:
  const Equip& prototype_;
  EquipStats scroll_stats_;
};

}  // namespace ms

#endif  // MS_SRC_EQUIP_INSTANCE_H_
