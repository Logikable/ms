#include "src/item/shop.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

namespace {

// The stocked equips worn in any of `slots`, in the order the shop list reads:
// the tier a player can reach now first, one kind of item together within
// that, and the cheaper of two of a kind ahead of the dearer. Name last, so the
// order never depends on how the catalog happens to be keyed.
std::vector<std::string> StockForSlots(
    const std::map<std::string, EquipPrototype>& equips,
    const std::vector<EquipSlot>& slots) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (entry.second.shop_price() <= 0 ||
        std::find(slots.begin(), slots.end(), entry.second.equip_slot()) ==
            slots.end()) {
      continue;
    }
    keys.push_back(entry.first);
  }
  std::sort(keys.begin(), keys.end(),
            [&equips](const std::string& a, const std::string& b) {
              const EquipPrototype& pa = equips.at(a);
              const EquipPrototype& pb = equips.at(b);
              if (pa.required_level() != pb.required_level()) {
                return pa.required_level() < pb.required_level();
              }
              if (pa.equip_type() != pb.equip_type()) {
                return pa.equip_type() < pb.equip_type();
              }
              if (pa.shop_price() != pb.shop_price()) {
                return pa.shop_price() < pb.shop_price();
              }
              return pa.name() < pb.name();
            });
  return keys;
}

}  // namespace

std::vector<std::string> ShopWeaponStock(
    const std::map<std::string, EquipPrototype>& equips) {
  // Stars sit in this list rather than one of their own: they are what a claw
  // swings, and a tab holding two items is not a tab.
  return StockForSlots(equips, {EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_STARS});
}

std::vector<std::string> ShopEtcStock(
    const std::map<std::string, ItemPrototype>& items) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, ItemPrototype>& entry : items) {
    if (entry.second.category() == ITEM_CATEGORY_ETC &&
        entry.second.shop_price() > 0) {
      keys.push_back(entry.first);
    }
  }
  std::sort(keys.begin(), keys.end(),
            [&items](const std::string& a, const std::string& b) {
              const ItemPrototype& pa = items.at(a);
              const ItemPrototype& pb = items.at(b);
              if (pa.shop_price() != pb.shop_price()) {
                return pa.shop_price() < pb.shop_price();
              }
              return pa.name() < pb.name();
            });
  return keys;
}

}  // namespace ms
