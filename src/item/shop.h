/* What the shop sells. Stock is not a list anyone maintains: an item joins the
 * shop by naming a shop_price in its own data file, and this reads that back
 * out of the catalog. So there is no second list to fall out of step with the
 * first, and no way to stock an item without saying what it costs.
 */
#ifndef MS_SRC_ITEM_SHOP_H_
#define MS_SRC_ITEM_SHOP_H_

#include <map>
#include <string>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {

// Catalog keys of everything the shop sells, in the order the columns of the
// shop list read: by equip slot, then required level, then job category, then
// name. Job order is the enum's -- warrior, bowman, magician, thief, then the
// universal items -- which is class order, not alphabetical.
std::vector<std::string> ShopStock(
    const std::map<std::string, EquipPrototype>& equips);

}  // namespace ms

#endif  // MS_SRC_ITEM_SHOP_H_
