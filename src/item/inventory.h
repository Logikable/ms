/* InventoryInstance holds the equip-tab items in a character's bag. It wraps a
 * vector of EquipTabItem and provides typed accessors so callers never need to
 * dynamic_cast manually. Mutation primitives are called by CharacterInstance;
 * high-level game logic stays there.
 */
#ifndef MS_SRC_ITEM_INVENTORY_H_
#define MS_SRC_ITEM_INVENTORY_H_

#include <functional>
#include <memory>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Slots on each of the bag's tabs: Equip, Use and Etc hold this many rows
// apiece. An equip takes a slot per copy; a stackable takes one per stack, so
// a tab holds this many stacks rather than this many items.
inline constexpr int kTabCapacity = 128;

class InventoryInstance {
 public:
  // Returns nullptr if index is out of range or the item is an EquipTrace.
  EquipInstance* equip_instance(int index);
  const EquipInstance* equip_instance(int index) const;

  // Raw item access for rendering. Index must be in range.
  const EquipTabItem& operator[](int index) const;

  int size() const;
  bool empty() const;

  // All EquipTrace items in the bag.
  std::vector<const EquipTrace*> traces() const;

  // Slots left on the equip tab.
  int room() const;
  bool full() const;

  // Mutation primitives.
  // Appends if index is -1; inserts before index otherwise. The caller checks
  // room() first: this is a primitive and does not refuse a full bag, so that
  // moving an item about internally cannot fail on a bag that is merely full.
  void add(std::unique_ptr<EquipTabItem> item, int index = -1);
  // Removes and returns the equip-tab item at index. Index must be in range.
  std::unique_ptr<EquipTabItem> remove_equip(int index);
  // Replaces the item at index. Index must be in range.
  void set(int index, std::unique_ptr<EquipTabItem> item);
  // Files the tab into the bag's sort order -- see inventory_sort.h.
  void Sort(const std::function<bool(const EquipPrototype&)>& equippable);

 private:
  std::vector<std::unique_ptr<EquipTabItem>> equip_items_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_INVENTORY_H_
