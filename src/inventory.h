/* InventoryInstance holds the equip-tab items in a character's bag. It wraps a
 * vector of EquipTabItem and provides typed accessors so callers never need to
 * dynamic_cast manually. Mutation primitives are called by CharacterInstance;
 * high-level game logic stays there.
 */
#ifndef MS_SRC_INVENTORY_H_
#define MS_SRC_INVENTORY_H_

#include <memory>
#include <vector>

#include "src/equip_instance.h"
#include "src/item.h"

namespace ms {

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

  // Mutation primitives.
  // Appends if index is -1; inserts before index otherwise.
  void add(std::unique_ptr<EquipTabItem> item, int index = -1);
  // Removes and returns the equip-tab item at index. Index must be in range.
  std::unique_ptr<EquipTabItem> remove_equip(int index);
  // Replaces the item at index. Index must be in range.
  void set(int index, std::unique_ptr<EquipTabItem> item);

 private:
  std::vector<std::unique_ptr<EquipTabItem>> equip_items_;
};

}  // namespace ms

#endif  // MS_SRC_INVENTORY_H_
