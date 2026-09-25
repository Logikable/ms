/* Whether an item can leave the character holding it, through a trade or into
 * the bank.
 *
 * Almost everything can. The two exceptions are currencies, which are balances
 * instead of rows and are transferred on their own line, and Arcane Symbols,
 * which are bound to the character who levelled them. Both screens check here
 * instead of listing the exceptions themselves.
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
