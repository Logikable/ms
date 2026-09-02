/* The bag's Sort: the order each tab is filed into when the player asks for
 * one. Both tabs sort in place, so the row a cursor stands on moves with the
 * rest and nothing outside holds an index across the call.
 */
#ifndef MS_SRC_ITEM_INVENTORY_SORT_H_
#define MS_SRC_ITEM_INVENTORY_SORT_H_

#include <functional>
#include <memory>
#include <vector>

#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Files the equip tab: what can be worn first, then the most stars, the most
// scrolls, the slot's place in the Equipped list, and the name. `equippable`
// answers whether the character may wear a prototype -- the one key the bag
// cannot answer by itself. A trace is never equippable, so the records of
// destroyed items gather below the live ones.
void SortEquipItems(
    std::vector<std::unique_ptr<EquipTabItem>>& items,
    const std::function<bool(const EquipPrototype&)>& equippable);

// Files a Use or Etc tab: Spell Traces, then the tokens a shop takes, then
// soul shards, then everything else, each by descending count. Ties go to the
// name, so a tab sorted twice comes out the same both times.
void SortStacks(std::vector<StackableItem>& stacks);

}  // namespace ms

#endif  // MS_SRC_ITEM_INVENTORY_SORT_H_
