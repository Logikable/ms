#ifndef MS_SRC_ITEM_H_
#define MS_SRC_ITEM_H_

#include <string>

#include "src/equip_stats.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Abstract base for all inventory items across all tabs.
class Item {
 public:
  virtual ~Item() = default;
  virtual const std::string& name() const = 0;
};

// Base for items on the equip tab: active equips and traces of destroyed items.
// Holds the shared prototype and per-instance state. StarForceStatGains() is
// defined in equip_instance.cc alongside the star force tables.
class EquipTabItem : public Item {
 public:
  // Default: returns the prototype name. EquipTrace overrides to append suffix.
  const std::string& name() const override {
    return prototype_.name();
  }
  const EquipPrototype& prototype() const {
    return prototype_;
  }
  const Equip& equip_state() const {
    return state_;
  }
  EquipStats StarForceStatGains() const;
  // Sum of prototype base stats, scroll stats, and star force stat gains.
  EquipStats stats() const;
  int stars() const {
    return state_.stars();
  }
  int max_stars() const {
    return MaxStarsForLevel(prototype_.required_level());
  }
  // Maximum star force level for the given required_level, per the GMS
  // equipment-level scaling table.
  static int MaxStarsForLevel(int required_level);

 protected:
  EquipTabItem(EquipPrototype prototype, Equip state)
      : prototype_(std::move(prototype)), state_(std::move(state)) {
  }
  EquipPrototype prototype_;
  Equip state_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_H_
