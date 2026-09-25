/* A slot's position in the game's single slot order: down the body, then
 * accessories, then carried items.
 *
 * Two lists use it (the Equipped panel draws worn gear in this order, and the
 * bag's Sort orders spare gear by it), so neither keeps its own order.
 */
#ifndef MS_SRC_ITEM_SLOT_ORDER_H_
#define MS_SRC_ITEM_SLOT_ORDER_H_

#include "src/protos/equip.pb.h"

namespace ms {

// A slot's position, lowest first: its family's position, then its position
// within the family, so the four rings stay together in fill order. A slot with
// no listed position sorts after all that have one.
int SlotOrder(EquipSlot slot);

}  // namespace ms

#endif  // MS_SRC_ITEM_SLOT_ORDER_H_
