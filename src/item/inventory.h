/* InventoryInstance holds the equip-tab items in a character's bag. It wraps a
 * vector of EquipTabItem and provides typed accessors so callers never need to
 * dynamic_cast. CharacterInstance calls the mutation methods; higher-level game
 * logic stays there.
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

class InventoryInstance {
 public:
  InventoryInstance() = default;
  // Deep copy: every item is cloned, so the copy and the original share
  // nothing.
  InventoryInstance(const InventoryInstance& other);
  InventoryInstance& operator=(const InventoryInstance& other);
  InventoryInstance(InventoryInstance&&) = default;
  InventoryInstance& operator=(InventoryInstance&&) = default;

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

  // Mutation methods. Appends if index is -1; otherwise inserts before index.
  // The caller checks room() first: this is a low-level method and doesn't
  // refuse a full bag, so moving an item around internally can't fail just
  // because the bag is full.
  void add(std::unique_ptr<EquipTabItem> item, int index = -1);
  // Removes and returns the equip-tab item at index. Index must be in range.
  std::unique_ptr<EquipTabItem> remove_equip(int index);
  // Replaces the item at index. Index must be in range.
  void set(int index, std::unique_ptr<EquipTabItem> item);
  // Sorts the tab into the bag's sort order; see inventory_sort.h.
  void Sort(const std::function<bool(const EquipPrototype&)>& equippable);

 private:
  std::vector<std::unique_ptr<EquipTabItem>> equip_items_;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_INVENTORY_H_
