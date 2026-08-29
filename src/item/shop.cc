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

// What the shop asks for one of these, on the shelf `payment` names.
int PriceOf(const EquipPrototype& proto, Payment payment) {
  return payment == kPaidInMeso ? proto.shop_price() : proto.token_price();
}

// Whether the item is on that shelf at all. A meso price is asked for by
// presence, because zero is a price there -- the Master Adventurer medal is
// free to anyone who asks for it. A token price is asked for by size: nothing
// is bought with no tokens.
bool Stocked(const EquipPrototype& proto, Payment payment) {
  return payment == kPaidInMeso ? proto.has_shop_price()
                                : proto.token_price() > 0;
}

// Whether a weapon shelf holds this slot. The other shelf holds everything
// else that is worn, so the two are one question asked both ways round and
// nothing can fall between them.
bool IsWeaponSlot(EquipSlot slot) {
  // Projectiles sit on the weapon shelf rather than one of their own: they are
  // what a claw or a bow swings, and a tab holding two items is not a tab.
  return slot == EQUIP_SLOT_PRIMARY_WEAPON || slot == EQUIP_SLOT_PROJECTILE;
}

// The stocked equips whose slot `on_shelf` claims, in the order the shop list
// reads: the tier a player can reach now first, one kind of item together
// within that, and the cheaper of two of a kind ahead of the dearer. Name
// last, so the order never depends on how the catalog happens to be keyed.
std::vector<std::string> StockForShelf(
    const std::map<std::string, EquipPrototype>& equips,
    bool (*on_shelf)(EquipSlot), Payment payment) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (!Stocked(entry.second, payment) ||
        !on_shelf(entry.second.equip_slot())) {
      continue;
    }
    keys.push_back(entry.first);
  }
  std::sort(keys.begin(), keys.end(),
            [&equips, payment](const std::string& a, const std::string& b) {
              const EquipPrototype& pa = equips.at(a);
              const EquipPrototype& pb = equips.at(b);
              if (pa.required_level() != pb.required_level()) {
                return pa.required_level() < pb.required_level();
              }
              if (pa.equip_type() != pb.equip_type()) {
                return pa.equip_type() < pb.equip_type();
              }
              if (PriceOf(pa, payment) != PriceOf(pb, payment)) {
                return PriceOf(pa, payment) < PriceOf(pb, payment);
              }
              return pa.name() < pb.name();
            });
  return keys;
}

}  // namespace

std::vector<std::string> ShopWeaponStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment) {
  return StockForShelf(equips, IsWeaponSlot, payment);
}

std::vector<std::string> ShopEquipStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment) {
  return StockForShelf(
      equips, [](EquipSlot slot) { return !IsWeaponSlot(slot); }, payment);
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
