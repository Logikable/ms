/* What the shop sells. There's no maintained stock list: an item is in the shop
 * when its own data file gives a price, and this reads that from the catalog.
 * So there's no second list to fall out of sync, and no way to stock an item
 * without pricing it.
 *
 * The weapon and equipment shelves each exist twice, once for meso and once for
 * tokens, because an item is priced in meso or in tokens, never both. It may
 * name several tokens, any one of which buys it.
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

// Whether `proto` belongs on a shelf for a job that fights with `weapons`: a
// weapon of one of those types, ammunition one of them draws, or anything that
// is neither. An empty list fits everything, which is how a 1st job, not yet in
// a branch, sees its whole category.
bool FitsWeapons(const EquipPrototype& proto,
                 const std::vector<EquipType>& weapons);

// Catalog keys of the stackables the shop sells, cheapest first, then by name.
// Stocking works as it does for equips: set a shop_price in the item's data
// file. Meso only: tokens buy equipment, not stackables.
std::vector<std::string> ShopEtcStock(
    const std::map<std::string, ItemPrototype>& items);

// What `proto` costs in `token`, a data file stem, or 0 when that token doesn't
// buy it.
int TokenPriceIn(const EquipPrototype& proto, const std::string& token);

// Whether `box` opens into `pick`: the token shelf sells it for the box's
// token, in one of the box's slots. Says nothing about who can wear it.
bool BoxHolds(const ItemPrototype& box, const EquipPrototype& pick);

// Catalog keys of everything `box` opens into, in the order the box lists its
// slots and in shop order within one slot. Empty for an item that isn't a box.
// Filtering by class is the caller's job, as for the shop.
std::vector<std::string> BoxStock(
    const ItemPrototype& box,
    const std::map<std::string, EquipPrototype>& equips);

}  // namespace ms

#endif  // MS_SRC_ITEM_SHOP_H_
