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
  // Sorted on the display name rather than the catalog key, because the name is
  // what the player is reading down.
  std::sort(keys.begin(), keys.end(),
            [&equips](const std::string& a, const std::string& b) {
              const EquipPrototype& pa = equips.at(a);
              const EquipPrototype& pb = equips.at(b);
              if (pa.required_level() != pb.required_level()) {
                return pa.required_level() < pb.required_level();
              }
              return pa.name() < pb.name();
            });
  return keys;
}

}  // namespace ms
