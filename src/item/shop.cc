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

// The shop's price for this item, on the shelf `payment` names.
int PriceOf(const EquipPrototype& proto, Payment payment) {
  return payment == kPaidInMeso ? proto.shop_price() : proto.token_price();
}

// Whether the item is on that shelf at all. A meso price is checked by
// presence, since zero is a valid price (the Master Adventurer medal is free).
// A token price is checked by size, since nothing costs zero tokens.
bool Stocked(const EquipPrototype& proto, Payment payment) {
  return payment == kPaidInMeso ? proto.has_shop_price()
                                : proto.token_price() > 0;
}

// Whether a weapon shelf holds this slot. The other shelf holds every other
// worn item, so both shelves use this one check and nothing can fall between
// them.
bool IsWeaponSlot(EquipSlot slot) {
  // Projectiles go on the weapon shelf instead of their own: a claw or bow uses
  // them, and a tab with two items isn't worth having.
  return slot == EQUIP_SLOT_PRIMARY_WEAPON || slot == EQUIP_SLOT_PROJECTILE;
}

// The stocked equips whose slot belongs to `on_shelf`, in shop list order: the
// tier the player can reach now first, items of one kind together within that,
// and the cheaper of two of a kind first. Name last, so the order never depends
// on the catalog's keys.
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
    if (entry.second.shop_price() > 0) {
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
