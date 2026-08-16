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

// What the shop asks for one of these, on the shelf `payment` names. Zero
// means it is not on that shelf at all.
int PriceOf(const EquipPrototype& proto, Payment payment) {
  return payment == kPaidInMeso ? proto.shop_price() : proto.token_price();
}

// The stocked equips worn in any of `slots`, in the order the shop list reads:
// the tier a player can reach now first, one kind of item together within
// that, and the cheaper of two of a kind ahead of the dearer. Name last, so the
// order never depends on how the catalog happens to be keyed.
std::vector<std::string> StockForSlots(
    const std::map<std::string, EquipPrototype>& equips,
    const std::vector<EquipSlot>& slots, Payment payment) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (PriceOf(entry.second, payment) <= 0 ||
        std::find(slots.begin(), slots.end(), entry.second.equip_slot()) ==
            slots.end()) {
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
  // Stars sit in this list rather than one of their own: they are what a claw
  // swings, and a tab holding two items is not a tab.
  return StockForSlots(equips, {EQUIP_SLOT_PRIMARY_WEAPON, EQUIP_SLOT_STARS},
                       payment);
}

std::vector<std::string> ShopSecondaryStock(
    const std::map<std::string, EquipPrototype>& equips, Payment payment) {
  return StockForSlots(equips, {EQUIP_SLOT_SECONDARY}, payment);
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
