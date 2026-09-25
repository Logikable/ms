/* The bag's Sort: the order each tab is put into when the player asks. Both
 * tabs sort in place, so the row under a cursor moves with the rest, and
 * nothing outside keeps an index across the call.
 */
#ifndef MS_SRC_ITEM_INVENTORY_SORT_H_
#define MS_SRC_ITEM_INVENTORY_SORT_H_

#include <functional>
#include <memory>
#include <vector>

#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Sorts the equip tab: wearable items first, then most stars, most scrolls, the
// slot's position in the Equipped list, and the name. `equippable` says whether
// the character can wear a prototype, the one key the bag can't work out
// itself. A trace is never wearable, so destroyed items collect below the live
// ones.
void SortEquipItems(
    std::vector<std::unique_ptr<EquipTabItem>>& items,
    const std::function<bool(const EquipPrototype&)>& equippable);

// Sorts a Use or Etc tab: Spell Traces, then shop tokens, then soul shards,
// then everything else, each by descending count. Ties sort by name, so sorting
// twice gives the same result.
void SortStacks(std::vector<StackableItem>& stacks);

}  // namespace ms

#endif  // MS_SRC_ITEM_INVENTORY_SORT_H_
