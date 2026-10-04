#include "src/item/ring_box.h"

#include <algorithm>
#include <map>
#include <random>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

const EquipPrototype* RingAt(
    const std::string& skill, int level,
    const std::map<std::string, EquipPrototype>& equips) {
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    const EquipmentSkill& granted = entry.second.equipment_skill();
    if (granted.skill() == skill && granted.level() == level) {
      return &entry.second;
    }
  }
  return nullptr;
}

bool RingBoxHolds(const ItemPrototype& box, const EquipPrototype& pick) {
  if (!box.has_ring_box()) {
    return false;
  }
  const RingBox& rings = box.ring_box();
  const EquipmentSkill& granted = pick.equipment_skill();
  bool listed = std::find(rings.skills().begin(), rings.skills().end(),
                          granted.skill()) != rings.skills().end();
  return listed && std::any_of(rings.levels().begin(), rings.levels().end(),
                               [&granted](const RingBox::LevelChance& chance) {
                                 return chance.level() == granted.level();
                               });
}

bool RingBoxHolds(const ItemPrototype& box, const std::string& item_key) {
  return box.has_ring_box() &&
         std::find(box.ring_box().items().begin(), box.ring_box().items().end(),
                   item_key) != box.ring_box().items().end();
}

int RollRingLevel(const RingBox& box, std::mt19937& rng) {
  double roll = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
  for (const RingBox::LevelChance& chance : box.levels()) {
    roll -= chance.chance();
    if (roll < 0.0) {
      return chance.level();
    }
  }
  // Rounding can leave a sliver past the last chance; it belongs to the last.
  return box.levels().empty() ? 0 : box.levels().rbegin()->level();
}

std::string RingLevelRange(const RingBox& box) {
  if (box.levels().empty()) {
    return "";
  }
  int low = box.levels(0).level();
  int high = low;
  for (const RingBox::LevelChance& chance : box.levels()) {
    low = std::min(low, chance.level());
    high = std::max(high, chance.level());
  }
  std::string text = "Lv. " + std::to_string(low);
  return low == high ? text : text + "-" + std::to_string(high);
}

}  // namespace ms
