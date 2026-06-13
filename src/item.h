#ifndef MS_SRC_ITEM_H_
#define MS_SRC_ITEM_H_

#include <string>

#include "src/protos/equip.pb.h"

namespace ms {

// Abstract base for all inventory items across all tabs.
class Item {
 public:
  virtual ~Item() = default;
  virtual const std::string& name() const = 0;
};

// Abstract base for items that occupy the equip tab (both active equips and
// traces of destroyed items). At most 128 unequipped instances may be held.
class EquipTabItem : public Item {
 public:
  virtual const EquipPrototype& prototype() const = 0;
  const std::string& name() const override {
    return prototype().name();
  }
};

}  // namespace ms

#endif  // MS_SRC_ITEM_H_
