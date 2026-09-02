#include "src/item/inventory_sort.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <tuple>
#include <vector>

#include "src/item/item.h"
#include "src/item/slot_order.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Where a stack's kind puts it, low first. An item that names no kind is an
// ordinary drop and files under all of them.
int KindRank(ItemKind kind) {
  switch (kind) {
    case ITEM_KIND_SPELL_TRACE:
      return 0;
    case ITEM_KIND_TOKEN:
      return 1;
    case ITEM_KIND_SOUL_SHARD:
      return 2;
    default:
      return 3;
  }
}

}  // namespace

void SortEquipItems(
    std::vector<std::unique_ptr<EquipTabItem>>& items,
    const std::function<bool(const EquipPrototype&)>& equippable) {
  // The keys read as "what the row is worth", so each one is negated to put
  // the best first while the comparison stays a plain ascending tuple.
  std::sort(items.begin(), items.end(),
            [&equippable](const std::unique_ptr<EquipTabItem>& a,
                          const std::unique_ptr<EquipTabItem>& b) {
              auto key = [&equippable](const EquipTabItem& item) {
                bool wearable =
                    !item.is_trace() && equippable(item.prototype());
                return std::make_tuple(wearable ? 0 : 1, -item.stars(),
                                       -item.equip_state().scroll_successes(),
                                       SlotOrder(item.prototype().equip_slot()),
                                       item.name());
              };
              return key(*a) < key(*b);
            });
}

void SortStacks(std::vector<StackableItem>& stacks) {
  std::sort(stacks.begin(), stacks.end(),
            [](const StackableItem& a, const StackableItem& b) {
              auto key = [](const StackableItem& stack) {
                return std::make_tuple(KindRank(stack.prototype().kind()),
                                       -stack.count(), stack.name());
              };
              return key(a) < key(b);
            });
}

}  // namespace ms
