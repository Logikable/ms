#include "src/commands/unequip.h"

#include <string>
#include <vector>

#include "src/character.h"
#include "src/commands/util.h"
#include "src/protos/equip.pb.h"
#include "src/frontend.h"

namespace ms {

std::string UnequipCommand(CharacterInstance& character, EquipSlot slot) {
  if (!character.Unequip(slot)) {
    return "Nothing equipped in that slot.";
  }
  return "Unequipped.";
}

void RegisterUnequipCommand(Frontend& frontend, CharacterInstance& character) {
  frontend.Register({
      "unequip",
      "Move the item in <slot> back to the bag.",
      [&character](std::vector<std::string> args) -> std::string {
        if (args.size() < 2) {
          return "Usage: /unequip <slot>";
        }
        EquipSlot slot = SlotFromName(args[1]);
        if (slot == EQUIP_SLOT_UNSPECIFIED) {
          return "Unknown slot '" + args[1] + "'.";
        }
        return UnequipCommand(character, slot);
      },
  });
}

}  // namespace ms
