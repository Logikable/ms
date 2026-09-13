/* Which stacks each tab of the bag lists.
 *
 * The bag holds every stackable in one vector; the tabs are views over it,
 * told apart by the item's kind. The Token tab draws two of them side by side
 * -- the shop's currencies and the bosses' soul shards -- and Etc is what is
 * left once those and the spell trace are out of it, the trace being drawn as
 * a balance in the tab bar rather than as a row anywhere.
 *
 * A view comes back as indices into the vector it was asked about, so a row on
 * a tab still names the one stack the bag holds and nothing has to keep a
 * second copy of anything.
 */
#ifndef MS_SRC_ITEM_STACK_TABS_H_
#define MS_SRC_ITEM_STACK_TABS_H_

#include <vector>

#include "src/item/item.h"

namespace ms {

enum class StackView {
  kEtc,
  kTokens,
  kSoulShards,
};

// The stacks `view` lists, in the order they sit in `stacks` -- which Sort has
// already filed by descending count within each kind.
std::vector<int> StacksIn(const std::vector<StackableItem>& stacks,
                          StackView view);

// Whether the player holds anything the Token tab would list. What opens the
// tab: a bar with a page that can only ever be empty on it is worse than one
// without.
bool HoldsCurrency(const std::vector<StackableItem>& stacks);

}  // namespace ms

#endif  // MS_SRC_ITEM_STACK_TABS_H_
