#include "src/character/dailies.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/boss_reset.h"
#include "src/character/hyper_stats.h"
#include "src/character/symbol.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

using SymbolKind = bool (*)(const EquipPrototype&);

// The furthest area of `kind` that `character` has a symbol from, worn or in
// the bag, or unspecified if they have none. The areas are unlocked in order
// and the slot enum follows that order, so comparing slots compares areas.
EquipSlot HighestSymbolOwned(const CharacterInstance& character,
                             SymbolKind kind) {
  EquipSlot highest = EQUIP_SLOT_UNSPECIFIED;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       character.equipped()) {
    if (kind(worn.second->prototype())) {
      highest = std::max(highest, worn.first);
    }
  }
  for (int i = 0; i < character.inventory().size(); ++i) {
    const EquipInstance* item = character.inventory().equip_instance(i);
    if (item != nullptr && kind(item->prototype())) {
      highest = std::max(highest, item->prototype().equip_slot());
    }
  }
  return highest;
}

// Whether the copies of `proto` that `character` holds, worn or in the bag,
// already add up to its max level, so another day's copies would be wasted.
bool HoldsEnoughToMax(const CharacterInstance& character,
                      const EquipPrototype& proto) {
  int held = 0;
  auto worn = character.equipped().find(proto.equip_slot());
  if (worn != character.equipped().end() &&
      IsSymbol(worn->second->prototype())) {
    held += SymbolWorth(worn->second->prototype(), worn->second->equip_state());
  }
  for (int worth : character.SpareSymbolWorths(proto.equip_slot())) {
    held += worth;
  }
  return held >= 1 + SymbolExpRoom(proto, 1);
}

}  // namespace

Equip PackedSymbol(const EquipPrototype& proto, int copies) {
  Equip state;
  // One copy is the item itself; the rest are stored in it as duplicates.
  state.set_symbol_exp(std::max(0, copies - 1));
  while (SymbolCanLevelUp(proto, state)) {
    LevelUpSymbol(proto, state);
  }
  return state;
}

int DailySymbolCopies(const EquipPrototype& proto) {
  return IsSacredSymbol(proto) ? proto.sacred_symbol().daily_copies()
                               : kSymbolsPerDay;
}

std::vector<const EquipPrototype*> ClaimableSymbols(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips) {
  const int stage = character.proto().job_stage();
  std::vector<std::pair<SymbolKind, bool>> kinds = {
      {&IsArcaneSymbol, stage >= kFifthJobStage},
      {&IsSacredSymbol, stage >= kSixthJobStage},
  };
  std::vector<const EquipPrototype*> claimable;
  for (const auto& [kind, open] : kinds) {
    EquipSlot highest = HighestSymbolOwned(character, kind);
    if (!open || highest == EQUIP_SLOT_UNSPECIFIED) {
      continue;
    }
    std::vector<const EquipPrototype*> of_kind;
    for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
      if (kind(entry.second) && entry.second.equip_slot() <= highest &&
          !HoldsEnoughToMax(character, entry.second)) {
        of_kind.push_back(&entry.second);
      }
    }
    std::sort(of_kind.begin(), of_kind.end(),
              [](const EquipPrototype* a, const EquipPrototype* b) {
                return a->equip_slot() < b->equip_slot();
              });
    claimable.insert(claimable.end(), of_kind.begin(), of_kind.end());
  }
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
    character.PickUp(std::make_unique<EquipInstance>(
        *proto, PackedSymbol(*proto, DailySymbolCopies(*proto))));
  }
  character.RecordDailiesClaim(now);
  return true;
}

}  // namespace ms
