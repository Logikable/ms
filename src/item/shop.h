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
#include "src/protos/item.pb.h"

namespace ms {

// Catalog keys of everything the shop sells, in the order the columns of the
// shop list read: by required level, then weapon type, then price, then name.
// Weapon order is the enum's, which keeps one kind of weapon together within a
// tier without claiming an order the player is meant to read anything into.
std::vector<std::string> ShopStock(
    const std::map<std::string, EquipPrototype>& equips);

// Catalog keys of the stackables the shop sells, cheapest first and then by
// name. Joining the shop works the same way it does for an equip: name a
// shop_price in the item's own data file and it is stocked.
std::vector<std::string> ShopEtcStock(
    const std::map<std::string, ItemPrototype>& items);

}  // namespace ms

#endif  // MS_SRC_ITEM_SHOP_H_
