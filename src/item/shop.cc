#include "src/item/shop.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The category an item sorts under: the first one it lists, with an item that
// lists none reading as universal, which is how the lists already display it.
//
// The enum runs in class order -- warrior, bowman, magician, thief, pirate,
// then the universal items -- and that is the order used here rather than the
// alphabet, because it is the order every other list in the game names the
// classes in.
EquipJobCategory SortCategory(const EquipPrototype& proto) {
  if (proto.equip_job_categories().empty()) {
    return EQUIP_JOB_CATEGORY_UNIVERSAL;
  }
  return proto.equip_job_categories(0);
}

}  // namespace

std::vector<std::string> ShopStock(
    const std::map<std::string, EquipPrototype>& equips) {
  std::vector<std::string> keys;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (entry.second.shop_price() > 0) {
      keys.push_back(entry.first);
    }
  }
  // Down the columns the list shows, left to right: everything worn in one
  // slot together, cheapest tier first within that, and one class's gear
  // together within that. Name last, so the order never depends on how the
  // catalog happens to be keyed.
  std::sort(keys.begin(), keys.end(),
            [&equips](const std::string& a, const std::string& b) {
              const EquipPrototype& pa = equips.at(a);
              const EquipPrototype& pb = equips.at(b);
              if (pa.equip_slot() != pb.equip_slot()) {
                return pa.equip_slot() < pb.equip_slot();
              }
              if (pa.required_level() != pb.required_level()) {
                return pa.required_level() < pb.required_level();
              }
              if (SortCategory(pa) != SortCategory(pb)) {
                return SortCategory(pa) < SortCategory(pb);
              }
              return pa.name() < pb.name();
            });
  return keys;
}

}  // namespace ms
