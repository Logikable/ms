#ifndef MS_SRC_ITEM_RING_BOX_H_
#define MS_SRC_ITEM_RING_BOX_H_

#include <map>
#include <random>
#include <string>

#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// The ring granting `skill` at `level`, or nullptr when the catalog has none.
const EquipPrototype* RingAt(
    const std::string& skill, int level,
    const std::map<std::string, EquipPrototype>& equips);

// Whether `box` is a ring box offering `pick`: a ring of a listed skill at a
// level the box can roll.
bool RingBoxHolds(const ItemPrototype& box, const EquipPrototype& pick);

// Whether `box` is a ring box offering the Etc item `item_key` instead.
bool RingBoxHolds(const ItemPrototype& box, const std::string& item_key);

// A level drawn from `box`'s chances.
int RollRingLevel(const RingBox& box, std::mt19937& rng);

// "Lv. 1-4": the levels `box` can roll.
std::string RingLevelRange(const RingBox& box);

}  // namespace ms

#endif  // MS_SRC_ITEM_RING_BOX_H_
