/* What the shop sells. Stock is not a list anyone maintains: an item joins the
 * shop by naming a price in its own data file, and this reads that back out of
 * the catalog. So there is no second list to fall out of step with the first,
 * and no way to stock an item without saying what it costs.
 *
 * A weapon shelf and an off-hand shelf are each stocked twice over -- once for
 * meso and once for tokens -- because an item names one price or the other and
 * never both.
 */
#ifndef MS_SRC_ITEM_SHOP_H_
#define MS_SRC_ITEM_SHOP_H_

#include <map>
#include <string>
#include <vector>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// What the shop asks for an item, which is what says which shelf it is on.
enum Payment { kPaidInMeso, kPaidInTokens };

// Catalog keys of the weapons the shop sells for `payment`, throwing stars
// included, in the order the columns of the shop list read: by required level,
// then weapon type, then price, then name. Weapon order is the enum's, which
// keeps one kind of weapon together within a tier without claiming an order the
// player is meant to read anything into.
std::vector<std::string> ShopWeaponStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment);

// Catalog keys of the off-hands the shop sells, in the same order. Class
// filtering is left to the caller, as it is for the weapons -- this says what
// is on the shelf, not who may buy it.
std::vector<std::string> ShopSecondaryStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment);

// Catalog keys of the stackables the shop sells, cheapest first and then by
// name. Joining the shop works the same way it does for an equip: name a
// shop_price in the item's own data file and it is stocked. Meso only: a token
// buys equipment, and a shelf of stackables is not what it is for.
std::vector<std::string> ShopEtcStock(
    const std::map<std::string, ItemPrototype>& items);

}  // namespace ms

#endif  // MS_SRC_ITEM_SHOP_H_
