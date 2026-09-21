/* Whether an item may leave the character holding it -- across a trade, or
 * into the bank.
 *
 * Almost everything may. The two that may not are a currency, which is a
 * balance rather than a row and crosses on a line of its own, and an Arcane
 * Symbol, which is bound to the character who raised it. Both screens ask
 * here rather than naming the exceptions themselves.
 */
#ifndef MS_SRC_ITEM_TRADEABLE_H_
#define MS_SRC_ITEM_TRADEABLE_H_

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

bool CanTrade(const ItemPrototype& proto);
bool CanTrade(const EquipPrototype& proto);

}  // namespace ms

#endif  // MS_SRC_ITEM_TRADEABLE_H_
