/* Where a slot sits in the game's one slot order: down the body, then the
 * accessories, then what is carried rather than worn.
 *
 * Two lists read it -- the Equipped panel draws worn gear in this order, and
 * the bag's Sort files spare gear by it -- so neither keeps an order of its
 * own.
 */
#ifndef MS_SRC_ITEM_SLOT_ORDER_H_
#define MS_SRC_ITEM_SLOT_ORDER_H_

#include "src/protos/equip.pb.h"

namespace ms {

// A slot's place, low first: its family's place, then its own place within the
// family, so the four rings stand together in the order they fill. A slot with
// no place named sorts after every slot that has one.
int SlotOrder(EquipSlot slot);

}  // namespace ms

#endif  // MS_SRC_ITEM_SLOT_ORDER_H_
