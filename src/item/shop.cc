#include "src/item/shop.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {

std::vector<std::string> ShopStock(
    const std::map<std::string, EquipPrototype>& equips) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (entry.second.shop_price() > 0) {
      keys.push_back(entry.first);
    }
  }
  // The order the shop list reads in: the tier a player can reach now first,
  // one kind of weapon together within that, and the cheaper of two of a kind
  // ahead of the dearer. Name last, so the order never depends on how the
  // catalog happens to be keyed.
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

}  // namespace ms
