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
#ifndef MS_SRC_ITEM_ITEM_H_
#define MS_SRC_ITEM_ITEM_H_

#include <map>
#include <string>
#include <vector>

#include "src/item/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// Display name of the spell trace, the currency scrolls are bought with. Named
// here because stackables are matched by display name wherever they cross a
// boundary, so the string is load-bearing in more than one place.
constexpr char kSpellTraceName[] = "Spell Trace";

// Whether `proto` accepts `upgrade` at all. This is the item's own answer,
// asked before any question about the state of a particular drop: a weapon
// that supports scrolling may still have no slots left, and one that supports
// star force may already be at max stars.
bool Supports(const EquipPrototype& proto, Upgrade upgrade);

// Whether `proto` has upgrade slots at all: it takes scrolls, and it drops
// carrying somewhere to put one. The question a golden hammer asks -- a hammer
// widens a shelf that exists, and cannot build one.
bool TakesUpgradeSlots(const EquipPrototype& proto);

// How many upgrade slots this drop holds: the prototype's, plus one for every
// golden hammer driven into it. Ask this rather than upgrade_slots wherever
// the number stands for the item's whole shelf -- what a clean slate may buy
// back, what a list writes after the slash.
int TotalUpgradeSlots(const EquipPrototype& proto, const Equip& state);

// What a shop pays for one of these, in meso. A stocked item sells for a
// tenth of what it charges, which is GMS's own buy-back rate and so is worked
// out rather than written down; an item no shop sells names its own price, and
// one that names nothing is worth nothing and is simply thrown away.
int SellPrice(const EquipPrototype& proto);

// The slots an item of this family may be worn in, in the order they fill.
// Pass any slot of the family: a ring answers with its four, a pendant with
// its two, and everything else with the one slot it is.
//
// A prototype names the first of its family -- EQUIP_SLOT_RING for every ring
// -- so the family is what turns what an item says it is into where it goes.
std::vector<EquipSlot> SlotFamily(EquipSlot slot);
// The slot a prototype of this family names, which is the first of them.
EquipSlot BaseSlot(EquipSlot slot);
// Where `slot` sits within its family, counting from zero.
int SlotIndex(EquipSlot slot);

// The catalog entry with this display name, or nullptr. Catalogs are keyed by
// data-file stem, so anything holding only a name -- a save, the shop's
// buy-back shelf -- has to come back in through here rather than by key.
const EquipPrototype* FindEquipByName(
    const std::map<std::string, EquipPrototype>& equips,
    const std::string& name);
const ItemPrototype* FindItemByName(
    const std::map<std::string, ItemPrototype>& items, const std::string& name);

// Abstract base for all inventory items across all tabs.
class Item {
 public:
  virtual ~Item() = default;
  virtual const std::string& name() const = 0;
};

// A stack of identical non-equip items (Use/Etc) in the inventory. Wraps a
// shared ItemPrototype with a per-stack count.
class StackableItem : public Item {
 public:
  StackableItem(ItemPrototype prototype, int count)
      : prototype_(std::move(prototype)), count_(count) {
  }

  const std::string& name() const override {
    return prototype_.name();
  }
  const ItemPrototype& prototype() const {
    return prototype_;
  }
  int count() const {
    return count_;
  }
  // Effective per-slot stack limit: prototype.max_stack() when set (> 0),
  // otherwise the category default (Use 9999, Etc 200).
  int max_stack() const;
  // Adds delta to the stack count. Callers keep the result within
  // [0, max_stack()]; the method does not clamp.
  void add_count(int delta);

 private:
  ItemPrototype prototype_;
  int count_;
};

// Base for items on the equip tab: active equips and traces of destroyed items.
// Holds the shared prototype and per-instance state.
class EquipTabItem : public Item {
 public:
  // What the stars alone add, the drop's own stats and its scrolls left out.
  // Pass stars >= 0 to ask about a level the item has not reached; -1, the
  // default, asks about the stars it has.
  //
  // What a star gives depends on where the item is worn: a weapon's attack and
  // MP climb, everything else's defense does, and Max HP goes to the slots on
  // GMS's Category A list.
  EquipStats StarForceStatGains(int stars = -1) const;
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
  // Whether this is the record of a destroyed item rather than an item that
  // still exists. Which of the two a row is lives in the C++ type, so nothing
  // reading the state alone can tell.
  virtual bool is_trace() const {
    return false;
  }
  // The state with that answer written into it, for anything that has to
  // rebuild the item later -- the save file, the shop's buy-back list. Not
  // folded into equip_state(), because a trace's state is also what recovery
  // copies to build the live item that replaces it.
  Equip SavedState() const;
  // The rolled lines this piece carries, empty until a cube goes into it. A
  // trace keeps what its item held: the lines are still worth reading, even
  // though nothing may reroll them again.
  const Potential& potential() const {
    return state_.main_potential();
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
  bool is_trace() const override {
    return true;
  }

 private:
  std::string display_name_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_ITEM_H_
