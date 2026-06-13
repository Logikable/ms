/* item.h defines the equip-tab item hierarchy shared across inventory
 * management, stat queries, and the inspect panel.
 *
 *   Item          — abstract base for all inventory items.
 *   EquipTabItem  — concrete base for equip-tab items (active and traces);
 *                   holds prototype + per-instance state; read-only queries.
 *                   Bodies live in item.cc.
 *   EquipTrace    — destroyed item snapshot; appends " Trace" to the name.
 *   EquipInstance — mutable subclass adding Scroll/StarForce; equip_instance.h.
 */
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
  // Returns stat gains contributed by star force levels alone (not
  // base/scroll).
  EquipStats StarForceStatGains() const;
  // Sum of prototype base stats, scroll stats, and star force stat gains.
  EquipStats stats() const;
  // Maximum star force level for the given required_level, per the GMS
  // equipment-level scaling table.
  static int MaxStarsForLevel(int required_level);

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
  int stars() const {
    return state_.stars();
  }
  int max_stars() const {
    return MaxStarsForLevel(prototype_.required_level());
  }

 protected:
  EquipTabItem(EquipPrototype prototype, Equip state)
      : prototype_(std::move(prototype)), state_(std::move(state)) {
  }
  EquipPrototype prototype_;
  Equip state_;
};

// A destroyed equipment item saved after a star force boom. Retains the full
// prototype and the item's state at the moment of destruction. Can be restored
// by combining with a fresh copy of the same base item.
class EquipTrace : public EquipTabItem {
 public:
  EquipTrace(EquipPrototype prototype, Equip state);
  const std::string& name() const override {
    return display_name_;
  }

 private:
  std::string display_name_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_H_
