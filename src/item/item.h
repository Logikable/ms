/* Defines the equip-tab item hierarchy used by inventory management, stat
 * queries and the inspect panel.
 *
 *   Item          — abstract base for all inventory items.
 *   EquipTabItem  — concrete base for equip-tab items (live items and traces);
 *                   holds prototype + per-item state; read-only queries.
 *                   Bodies are in item.cc.
 *   EquipTrace    — snapshot of a destroyed item; appends " Trace" to the name.
 *   EquipInstance — mutable subclass adding Scroll/StarForce; equip_instance.h.
 */
#ifndef MS_SRC_ITEM_ITEM_H_
#define MS_SRC_ITEM_ITEM_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/item/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// Slots on each bag tab: Equip and Etc each hold this many rows, as does each
// bank tab. An equip takes one slot per copy; a stackable takes one per stack,
// so a tab holds this many stacks, not this many items.
inline constexpr int kTabCapacity = 128;

// Display name of the spell trace, the currency scrolls are bought with.
// Defined here because stackables are matched by display name wherever they
// cross a boundary, so several places depend on this exact string.
constexpr char kSpellTraceName[] = "Spell Trace";

// The name a list shows for `proto` when its column header already says the
// rest: a soul shard shows "Zakum's" under a Soul Shard column. Falls back to
// the full name, which is all most items have.
const std::string& ShortName(const ItemPrototype& proto);

// Whether `proto` allows `upgrade` at all. This is about the item type, checked
// before any question about a particular copy: a weapon that supports scrolling
// may still have no slots left, and one that supports star force may already be
// at max stars.
bool Supports(const EquipPrototype& proto, Upgrade upgrade);

// Whether `proto` has upgrade slots at all: it takes scrolls and drops with
// somewhere to put one. A golden hammer checks this, since it widens existing
// slots and can't create them.
bool TakesUpgradeSlots(const EquipPrototype& proto);

// How many upgrade slots this item has: the prototype's, plus one for each
// golden hammer used on it. Use this instead of upgrade_slots wherever the
// number means the item's total slots, such as what a Clean Slate can restore
// or the number a list shows after the slash.
int TotalUpgradeSlots(const EquipPrototype& proto, const Equip& state);

// What a shop pays for one of these, in meso. A shop-stocked item sells for a
// tenth of its price, GMS's buy-back rate, so it's computed instead of stored.
// An item no shop sells states its own price, and one stating none is worth
// nothing and is simply discarded.
int SellPrice(const EquipPrototype& proto);

// The slots an item of this family can be worn in, in fill order. Pass any slot
// of the family. A prototype names the first slot of its family, so this turns
// what an item says it is into where it can go.
std::vector<EquipSlot> SlotFamily(EquipSlot slot);
// The slot a prototype of this family names, which is the first one.
EquipSlot BaseSlot(EquipSlot slot);
// The position of `slot` within its family, counting from zero.
int SlotIndex(EquipSlot slot);

// Catalogs are keyed by data file stem ("sword"), while a saved item uses its
// display name ("Sword"). This index maps between the two, which is why a save
// can't look an item up in the catalog directly.
template <typename Proto>
std::map<std::string, const Proto*> IndexByDisplayName(
    const std::map<std::string, Proto>& catalog) {
  std::map<std::string, const Proto*> by_name;
  for (const std::pair<const std::string, Proto>& entry : catalog) {
    by_name[entry.second.name()] = &entry.second;
  }
  return by_name;
}

// The catalog entry with this display name, or nullptr. Catalogs are keyed by
// data file stem, so anything with only a name (a save, the shop's buy-back
// shelf) must look items up through here.
const EquipPrototype* FindEquipByName(
    const std::map<std::string, EquipPrototype>& equips,
    const std::string& name);
const ItemPrototype* FindItemByName(
    const std::map<std::string, ItemPrototype>& items, const std::string& name);

// Sets each token's currency_level and currency_slot from what `equips` sells
// for it: the highest level it buys, and the slot if it buys only one slot.
// Computed instead of stored in data so the two can't drift apart.
void FillTokenShelves(const std::map<std::string, EquipPrototype>& equips,
                      std::map<std::string, ItemPrototype>& items);

// Abstract base for all inventory items across all tabs.
class Item {
 public:
  virtual ~Item() = default;
  virtual const std::string& name() const = 0;
};

// A stack of identical non-equip items in the inventory. Wraps a shared
// ItemPrototype with a per-stack count.
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
  // The stack limit: prototype.max_stack() when set (> 0), otherwise 200.
  int max_stack() const;
  // Adds delta to the stack count. Callers keep the result within [0,
  // max_stack()]; this doesn't clamp.
  void add_count(int delta);

 private:
  ItemPrototype prototype_;
  int count_;
};

// Base for equip-tab items: live equipment and traces of destroyed items. Holds
// the shared prototype and per-item state.
class EquipTabItem : public Item {
 public:
  // What the stars alone add, excluding the item's own stats and scrolls. Pass
  // stars >= 0 to ask about a star level not yet reached; -1 means the current
  // level. What a star gives depends on where the item is worn.
  EquipStats StarForceStatGains(int stars = -1) const;
  // Sum of prototype base stats, scroll stats, and star force stat gains.
  EquipStats stats() const;
  // Maximum star force level for the given required_level, per the GMS
  // equipment-level scaling table.
  static int MaxStarsForLevel(int required_level);

  // By default returns the prototype name. EquipTrace overrides it to add a
  // suffix.
  const std::string& name() const override {
    return prototype_.name();
  }
  const EquipPrototype& prototype() const {
    return prototype_;
  }
  const Equip& equip_state() const {
    return state_;
  }
  // Whether this is the record of a destroyed item instead of a live one. This
  // is stored in the C++ type, so nothing reading the state alone can tell.
  virtual bool is_trace() const {
    return false;
  }
  // A copy of the same type, for a bag that holds items by pointer.
  virtual std::unique_ptr<EquipTabItem> Clone() const = 0;
  // The state with the trace flag written in, for anything that rebuilds the
  // item later (the save file, the shop's buy-back list). Not part of
  // equip_state(), because a trace's state is also what recovery copies to
  // build the live item that replaces it.
  Equip SavedState() const;
  // The potential lines on this item, empty until it's cubed. A trace keeps its
  // item's lines: they're still worth reading, though nothing can reroll them.
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

// A destroyed item, saved after a star force destruction. Keeps the full
// prototype and the item's state when it was destroyed. Can be restored by
// combining it with a new copy of the same base item.
class EquipTrace : public EquipTabItem {
 public:
  EquipTrace(EquipPrototype prototype, Equip state);
  const std::string& name() const override {
    return display_name_;
  }
  bool is_trace() const override {
    return true;
  }
  std::unique_ptr<EquipTabItem> Clone() const override;

 private:
  std::string display_name_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_ITEM_H_
