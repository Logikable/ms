/* What the shop sells. There's no maintained stock list: an item is in the shop
 * when its own data file gives a price, and this reads that from the catalog.
 * So there's no second list to fall out of sync, and no way to stock an item
 * without pricing it.
 *
 * The weapon and equipment shelves each exist twice, once for meso and once for
 * tokens, because an item gives one price or the other, never both.
 */
#ifndef MS_SRC_ITEM_SHOP_H_
#define MS_SRC_ITEM_SHOP_H_

#include <map>
#include <string>
#include <vector>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// How the shop charges for an item, which determines its shelf.
enum Payment { kPaidInMeso, kPaidInTokens };

// Catalog keys of the weapons the shop sells for `payment`, including throwing
// stars, in the shop list's column order: by required level, then weapon type,
// then price, then name. Weapon order follows the enum, which keeps one weapon
// type together within a tier without implying any meaningful order.
std::vector<std::string> ShopWeaponStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment);

// Catalog keys of everything else the shop sells: secondaries, and the rings,
// emblems and medals next to them, meaning everything worn that isn't a weapon
// or thrown, so the two shelves never overlap. Filtering by class is the
// caller's job: this says what's on the shelf, not who can buy it.
std::vector<std::string> ShopEquipStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment);

// Catalog keys of the stackables the shop sells, cheapest first, then by name.
// Stocking works as it does for equips: set a shop_price in the item's data
// file. Meso only: tokens buy equipment, not stackables.
std::vector<std::string> ShopEtcStock(
    const std::map<std::string, ItemPrototype>& items);

}  // namespace ms

#endif  // MS_SRC_ITEM_SHOP_H_
