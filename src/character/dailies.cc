#include "src/character/dailies.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/boss_reset.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// Whether `slot` is `highest` or an area before it. The areas are unlocked in
// order and the slot enum follows that order, so comparing slots compares
// areas.
bool Below(EquipSlot slot, EquipSlot highest) {
  return slot <= highest;
}

// The furthest area `character` has a symbol from, worn or in the bag, or
// unspecified if they have none.
EquipSlot HighestSymbolOwned(const CharacterInstance& character) {
  EquipSlot highest = EQUIP_SLOT_UNSPECIFIED;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       character.equipped()) {
    if (IsArcaneSymbol(worn.second->prototype())) {
      highest = std::max(highest, worn.first);
    }
  }
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipInstance* item = character.inventory().equip_instance(i);
    if (item != nullptr && IsArcaneSymbol(item->prototype())) {
      highest = std::max(highest, item->prototype().equip_slot());
    }
  }
  return highest;
}

}  // namespace

Equip PackedSymbol(int copies) {
  Equip state;
  // One copy is the item itself; the rest are stored in it as duplicates.
  state.set_symbol_exp(std::max(0, copies - 1));
  while (SymbolCanLevelUp(state)) {
    LevelUpSymbol(state);
  }
  return state;
}

std::vector<const EquipPrototype*> ClaimableSymbols(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips) {
  EquipSlot highest = HighestSymbolOwned(character);
  std::vector<const EquipPrototype*> claimable;
  if (highest == EQUIP_SLOT_UNSPECIFIED) {
    return claimable;
  }
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    if (IsArcaneSymbol(entry.second) &&
        Below(entry.second.equip_slot(), highest)) {
      claimable.push_back(&entry.second);
    }
  }
  std::sort(claimable.begin(), claimable.end(),
            [](const EquipPrototype* a, const EquipPrototype* b) {
              return a->equip_slot() < b->equip_slot();
            });
  return claimable;
}

bool DailiesAvailable(int64_t claimed, int64_t now) {
  return BossAvailable(claimed, RESET_PERIOD_DAILY, now);
}

bool ClaimDailies(CharacterInstance& character,
                  const std::map<std::string, EquipPrototype>& equips,
                  int64_t now) {
  std::vector<const EquipPrototype*> claimable =
      ClaimableSymbols(character, equips);
  if (claimable.empty() ||
      !DailiesAvailable(character.DailiesClaimedAt(), now) ||
      character.inventory().room() < static_cast<int>(claimable.size())) {
    return false;
  }
  for (const EquipPrototype* proto : claimable) {
    character.PickUp(
        std::make_unique<EquipInstance>(*proto, PackedSymbol(kSymbolsPerDay)));
  }
  character.RecordDailiesClaim(now);
  return true;
}

}  // namespace ms
