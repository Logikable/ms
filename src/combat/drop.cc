#include "src/combat/drop.h"

#include <map>
#include <string>

#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

std::string DropName(const GameState& state, const MobDrop& drop) {
  if (drop.has_equip()) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(drop.equip());
    return it == state.equips.end() ? "" : it->second.name();
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  return it == state.items.end() ? "" : it->second.name();
}

bool DropIsPrize(const GameState& state, const MobDrop& drop) {
  if (drop.has_equip()) {
    return true;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      state.items.find(drop.item());
  if (it == state.items.end()) {
    return false;
  }
  return it->second.kind() == ITEM_KIND_TOKEN ||
         it->second.opens_into_case() != ItemPrototype::OPENS_INTO_NOT_SET ||
         state.scrolls.count(drop.item()) > 0;
}

}  // namespace ms
